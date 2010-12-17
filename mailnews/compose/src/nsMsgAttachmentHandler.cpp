/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsMsgAttachmentHandler.h"
#include "prmem.h"
#include "nsMsgCopy.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsMsgSend.h"
#include "nsMsgCompUtils.h"
#include "nsMsgEncoders.h"
#include "nsMsgI18N.h"
#include "nsURLFetcher.h"
#include "nsMimeTypes.h"
#include "nsMsgCompCID.h"
#include "nsReadableUtils.h"
#include "nsIMsgMessageService.h"
#include "nsMsgUtils.h"
#include "nsMsgPrompts.h"
#include "nsTextFormatter.h"
#include "nsIPrompt.h"
#include "nsITextToSubURI.h"
#include "nsEscape.h"
#include "nsIURL.h"
#include "nsIFileURL.h"
#include "nsNetCID.h"
#include "nsIMimeStreamConverter.h"
#include "nsMsgMimeCID.h"
#include "nsNetUtil.h"
#include "nsNativeCharsetUtils.h"
#include "nsComposeStrings.h"
#include "nsIZipWriter.h"
#include "nsIDirectoryEnumerator.h"

///////////////////////////////////////////////////////////////////////////
// Mac Specific Attachment Handling for AppleDouble Encoded Files
///////////////////////////////////////////////////////////////////////////
#ifdef XP_MACOSX

#define AD_WORKING_BUFF_SIZE                  8192

extern void         MacGetFileType(nsILocalFile *fs, PRBool *useDefault, char **type, char **encoding);

#include "nsILocalFileMac.h"

/* static */
nsresult nsSimpleZipper::Zip(nsIFile *aInputFile, nsIFile *aOutputFile)
{
  // create zipwriter object
  nsresult rv;
  nsCOMPtr<nsIZipWriter> zipWriter = do_CreateInstance("@mozilla.org/zipwriter;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  
  rv = zipWriter->Open(aOutputFile, PR_RDWR | PR_CREATE_FILE | PR_TRUNCATE);
  NS_ENSURE_SUCCESS(rv, rv); 
  
  rv = AddToZip(zipWriter, aInputFile, EmptyCString());
  NS_ENSURE_SUCCESS(rv, rv);

  // we're done.
  zipWriter->Close();
  return rv;
}

/* static */
nsresult nsSimpleZipper::AddToZip(nsIZipWriter *aZipWriter, 
                                  nsIFile *aFile,
                                  const nsACString &aPath)
{
  // find out the path this file/dir should have in the zip
  nsCString leafName;
  aFile->GetNativeLeafName(leafName);
  nsCString currentPath(aPath + leafName);
    
  PRBool isDirectory;
  aFile->IsDirectory(&isDirectory);
  // append slash for a directory entry
  if (isDirectory)
    currentPath.Append('/');
  
  // add the file or directory entry to the zip
  nsresult rv = aZipWriter->AddEntryFile(currentPath, nsIZipWriter::COMPRESSION_DEFAULT, aFile, PR_FALSE);
  NS_ENSURE_SUCCESS(rv, rv);
  
  // if it's a directory, add all its contents too
  if (isDirectory) {
    nsCOMPtr<nsISimpleEnumerator> e;
    nsresult rv = aFile->GetDirectoryEntries(getter_AddRefs(e));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIDirectoryEnumerator> dirEnumerator = do_QueryInterface(e, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIFile> currentFile;
    while (NS_SUCCEEDED(dirEnumerator->GetNextFile(getter_AddRefs(currentFile))) && currentFile) {
      rv = AddToZip(aZipWriter, currentFile, currentPath);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  
  return NS_OK;
}
#endif // XP_MACOSX

//
// Class implementation...
//
nsMsgAttachmentHandler::nsMsgAttachmentHandler() :
  mRequest(nsnull),
  mCompFields(nsnull),   // Message composition fields for the sender
  
  m_x_mac_type(nsnull),
  m_x_mac_creator(nsnull),

  m_done(PR_FALSE),
  m_charset(nsnull),
  m_content_id(nsnull),
  m_type(nsnull),
  m_type_param(nsnull),
  m_override_type(nsnull),
  m_override_encoding(nsnull),
  m_desired_type(nsnull),
  m_description(nsnull),
  m_real_name(nsnull),
  m_encoding(nsnull),
  m_already_encoded_p(PR_FALSE),
  m_decrypted_p(PR_FALSE),
  
  mDeleteFile(PR_FALSE),
  mMHTMLPart(PR_FALSE),
  mPartUserOmissionOverride(PR_FALSE),
  mMainBody(PR_FALSE),

  // For analyzing the attachment file...
  m_size(0),
  m_unprintable_count(0),
  m_highbit_count(0),
  m_ctl_count(0),
  m_null_count(0),
  m_have_cr(0), 
  m_have_lf(0), 
  m_have_crlf(0),
  m_prev_char_was_cr(PR_FALSE),
  m_current_column(0),
  m_max_column(0),
  m_lines(0),
  m_file_analyzed(PR_FALSE),

  // Mime
  m_encoder_data(nsnull),
  m_uri(nsnull)
{
}

nsMsgAttachmentHandler::~nsMsgAttachmentHandler()
{
  if (mTmpFile && mDeleteFile)
    mTmpFile->Remove(PR_FALSE);
    
  if (mOutFile)
    mOutFile->Close();
    
  CleanupTempFile();

  PR_Free(m_charset);
  PR_Free(m_type);
  PR_Free(m_type_param);
  PR_Free(m_content_id);
  PR_Free(m_desired_type);
  PR_Free(m_encoding);
  PR_Free(m_override_type);
  PR_Free(m_description);
  PR_Free(m_real_name);
  PR_Free(m_override_encoding);
  PR_Free(m_x_mac_type);
  PR_Free(m_x_mac_creator);
  PR_Free(m_uri);
}

void
nsMsgAttachmentHandler::CleanupTempFile()
{
#ifdef XP_MACOSX
  if (mEncodedWorkingFile) {
    mEncodedWorkingFile->Remove(PR_FALSE);
    mEncodedWorkingFile = nsnull;
  }
#endif // XP_MACOSX
}

void
nsMsgAttachmentHandler::AnalyzeDataChunk(const char *chunk, PRInt32 length)
{
  unsigned char *s = (unsigned char *) chunk;
  unsigned char *end = s + length;
  for (; s < end; s++)
  {
    if (*s > 126)
    {
      m_highbit_count++;
      m_unprintable_count++;
    }
    else if (*s < ' ' && *s != '\t' && *s != '\r' && *s != '\n')
    {
      m_unprintable_count++;
      m_ctl_count++;
      if (*s == 0)
        m_null_count++;
    }

    if (*s == '\r' || *s == '\n')
    {
      if (*s == '\r')
      {
        if (m_prev_char_was_cr)
          m_have_cr = 1;
        else
          m_prev_char_was_cr = PR_TRUE;
      }
      else
      {
        if (m_prev_char_was_cr)
        {
          if (m_current_column == 0)
          {
            m_have_crlf = 1;
            m_lines--;
          }
          else
            m_have_cr = m_have_lf = 1;
          m_prev_char_was_cr = PR_FALSE;
        }
        else
          m_have_lf = 1;
      }
      if (m_max_column < m_current_column)
        m_max_column = m_current_column;
      m_current_column = 0;
      m_lines++;
    }
    else
    {
      m_current_column++;
    }
  }
}

void
nsMsgAttachmentHandler::AnalyzeSnarfedFile(void)
{
  char chunk[1024];
  PRUint32 numRead = 0;

  if (m_file_analyzed)
    return;

  if (mTmpFile)
  {
    PRInt64 fileSize;
    mTmpFile->GetFileSize(&fileSize);
    m_size = (PRUint32) fileSize;
    nsCOMPtr <nsIInputStream> inputFile;
    nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputFile), mTmpFile);
    if (NS_FAILED(rv))
      return;
    {
      do
      {
        rv = inputFile->Read(chunk, sizeof(chunk), &numRead);
        if (numRead)
          AnalyzeDataChunk(chunk, numRead);
      }
      while (numRead && NS_SUCCEEDED(rv));
      if (m_prev_char_was_cr)
        m_have_cr = 1;

      inputFile->Close();
      m_file_analyzed = PR_TRUE;
    }
  }
}

//
// Given a content-type and some info about the contents of the document,
// decide what encoding it should have.
//
int
nsMsgAttachmentHandler::PickEncoding(const char *charset, nsIMsgSend *mime_delivery_state)
{
  nsCOMPtr<nsIPrefBranch> pPrefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));

  PRBool needsB64 = PR_FALSE;
  PRBool forceB64 = PR_FALSE;

  if (m_already_encoded_p)
    goto DONE;

  AnalyzeSnarfedFile();

  /* Allow users to override our percentage-wise guess on whether
  the file is text or binary */
  if (pPrefBranch)
    pPrefBranch->GetBoolPref ("mail.file_attach_binary", &forceB64);

  if (!mMainBody && (forceB64 || mime_type_requires_b64_p (m_type) ||
    m_have_cr+m_have_lf+m_have_crlf != 1 || m_current_column != 0))
  {
  /* If the content-type is "image/" or something else known to be binary
  or several flavors of newlines are present or last line is incomplete,
  always use base64 (so that we don't get confused by newline
  conversions.)
     */
    needsB64 = PR_TRUE;
  }
  else
  {
  /* Otherwise, we need to pick an encoding based on the contents of
  the document.
     */

    PRBool encode_p;
    PRBool force_p = PR_FALSE;

    /*
      force quoted-printable if the sender does not allow
      conversion to 7bit
    */
    if (mCompFields) {
      if (mCompFields->GetForceMsgEncoding())
        force_p = PR_TRUE;
    }
    else if (mime_delivery_state) {
      if (((nsMsgComposeAndSend *)mime_delivery_state)->mCompFields->GetForceMsgEncoding())
        force_p = PR_TRUE;
    }

    if (force_p || (m_max_column > 900))
      encode_p = PR_TRUE;
    else if (UseQuotedPrintable() && m_unprintable_count)
      encode_p = PR_TRUE;

      else if (m_null_count)  /* If there are nulls, we must always encode,
        because sendmail will blow up. */
        encode_p = PR_TRUE;
      else
        encode_p = PR_FALSE;

        /* MIME requires a special case that these types never be encoded.
      */
      if (!PL_strncasecmp (m_type, "message", 7) ||
        !PL_strncasecmp (m_type, "multipart", 9))
      {
        encode_p = PR_FALSE;
        if (m_desired_type && !PL_strcasecmp (m_desired_type, TEXT_PLAIN))
        {
          PR_Free (m_desired_type);
          m_desired_type = 0;
        }
      }

      // If the Mail charset is multibyte, we force it to use Base64 for attachments.
      if ((!mMainBody && charset && nsMsgI18Nmultibyte_charset(charset)) &&
        ((PL_strcasecmp(m_type, TEXT_HTML) == 0) ||
        (PL_strcasecmp(m_type, TEXT_MDL) == 0) ||
        (PL_strcasecmp(m_type, TEXT_PLAIN) == 0) ||
        (PL_strcasecmp(m_type, TEXT_RICHTEXT) == 0) ||
        (PL_strcasecmp(m_type, TEXT_ENRICHED) == 0) ||
        (PL_strcasecmp(m_type, TEXT_VCARD) == 0) ||
        (PL_strcasecmp(m_type, APPLICATION_DIRECTORY) == 0) || /* text/x-vcard synonym */
        (PL_strcasecmp(m_type, TEXT_CSS) == 0) ||
        (PL_strcasecmp(m_type, TEXT_JSSS) == 0)))
      {
        needsB64 = PR_TRUE;
      }
      else if (charset && nsMsgI18Nstateful_charset(charset))
      {
        PR_Free(m_encoding);
        m_encoding = PL_strdup (ENCODING_7BIT);
      }
      else if (encode_p &&
        m_unprintable_count > (m_size / 10))
        /* If the document contains more than 10% unprintable characters,
        then that seems like a good candidate for base64 instead of
        quoted-printable.
        */
        needsB64 = PR_TRUE;
      else if (encode_p) {
        PR_Free(m_encoding);
        m_encoding = PL_strdup (ENCODING_QUOTED_PRINTABLE);
      }
      else if (m_highbit_count > 0) {
        PR_Free(m_encoding);
        m_encoding = PL_strdup (ENCODING_8BIT);
      }
      else {
        PR_Free(m_encoding);
        m_encoding = PL_strdup (ENCODING_7BIT);
      }
  }

  if (needsB64)
  {
    //
    // always base64 binary data.
    //
    PR_Free(m_encoding);
    m_encoding = PL_strdup (ENCODING_BASE64);
  }

  /* Now that we've picked an encoding, initialize the filter.
  */
  NS_ASSERTION(!m_encoder_data, "not-null m_encoder_data");
  if (!PL_strcasecmp(m_encoding, ENCODING_BASE64))
  {
    m_encoder_data = MIME_B64EncoderInit(mime_encoder_output_fn,
      mime_delivery_state);
    if (!m_encoder_data) return NS_ERROR_OUT_OF_MEMORY;
  }
  else if (!PL_strcasecmp(m_encoding, ENCODING_QUOTED_PRINTABLE))
  {
    m_encoder_data = MIME_QPEncoderInit(mime_encoder_output_fn,
      mime_delivery_state);
    if (!m_encoder_data) return NS_ERROR_OUT_OF_MEMORY;
  }
  else
  {
    m_encoder_data = 0;
  }

  /* Do some cleanup for documents with unknown content type.
    There are two issues: how they look to MIME users, and how they look to
    non-MIME users.

      If the user attaches a "README" file, which has unknown type because it
      has no extension, we still need to send it with no encoding, so that it
      is readable to non-MIME users.

        But if the user attaches some random binary file, then base64 encoding
        will have been chosen for it (above), and in this case, it won't be
        immediately readable by non-MIME users.  However, if we type it as
        text/plain instead of application/octet-stream, it will show up inline
        in a MIME viewer, which will probably be ugly, and may possibly have
        bad charset things happen as well.

          So, the heuristic we use is, if the type is unknown, then the type is
          set to application/octet-stream for data which needs base64 (binary data)
          and is set to text/plain for data which didn't need base64 (unencoded or
          lightly encoded data.)
  */
DONE:
  if (!m_type || !*m_type || !PL_strcasecmp(m_type, UNKNOWN_CONTENT_TYPE))
  {
    PR_Free(m_type);
    if (m_already_encoded_p)
      m_type = PL_strdup (APPLICATION_OCTET_STREAM);
    else if (m_encoding &&
         (!PL_strcasecmp(m_encoding, ENCODING_BASE64) ||
         !PL_strcasecmp(m_encoding, ENCODING_UUENCODE)))
         m_type = PL_strdup (APPLICATION_OCTET_STREAM);
    else
      m_type = PL_strdup (TEXT_PLAIN);
  }
  return 0;
}

static nsresult
FetcherURLDoneCallback(nsresult aStatus,
                       const nsACString &aContentType,
                       const nsACString &aCharset,
                       PRInt32 totalSize,
                       const PRUnichar* aMsg, void *tagData)
{
  nsMsgAttachmentHandler *ma = (nsMsgAttachmentHandler *) tagData;
  NS_ASSERTION(ma != nsnull, "not-null mime attachment");

  if (ma != nsnull)
  {
    ma->m_size = totalSize;
    if (!aContentType.IsEmpty())
    {
#ifdef XP_MACOSX
      //Do not change the type if we are dealing with an encoded (e.g., appledouble or zip) file
      if (!ma->mEncodedWorkingFile)
#else
        // can't send appledouble on non-macs
        if (!aContentType.EqualsLiteral("multipart/appledouble"))
#endif
      {
        PR_Free(ma->m_type);
        ma->m_type = ToNewCString(aContentType);
      }
    }

    if (!aCharset.IsEmpty())
    {
      PR_Free(ma->m_charset);
      ma->m_charset = ToNewCString(aCharset);
    }

    return ma->UrlExit(aStatus, aMsg);
  }
  else
    return NS_OK;
}

nsresult
nsMsgAttachmentHandler::SnarfMsgAttachment(nsMsgCompFields *compFields)
{
  nsresult rv = NS_ERROR_INVALID_ARG;
  nsCOMPtr <nsIMsgMessageService> messageService;

  if (PL_strcasestr(m_uri, "-message:"))
  {
    nsCOMPtr <nsIFile> tmpFile;
    rv = nsMsgCreateTempFile("nsmail.tmp", getter_AddRefs(tmpFile));
    NS_ENSURE_SUCCESS(rv, rv);
    mTmpFile = do_QueryInterface(tmpFile);
    mDeleteFile = PR_TRUE;
    mCompFields = compFields;
    PR_Free(m_type);
    m_type = PL_strdup(MESSAGE_RFC822);
    PR_Free(m_override_type);
    m_override_type = PL_strdup(MESSAGE_RFC822);
    if (!mTmpFile)
    {
      rv = NS_ERROR_FAILURE;
      goto done;
    }

    rv = MsgNewBufferedFileOutputStream(getter_AddRefs(mOutFile), mTmpFile, -1, 00600);
    if (NS_FAILED(rv) || !mOutFile)
    {
      if (m_mime_delivery_state)
      {
        nsCOMPtr<nsIMsgSendReport> sendReport;
        m_mime_delivery_state->GetSendReport(getter_AddRefs(sendReport));
        if (sendReport)
        {
          nsAutoString error_msg;
          nsMsgBuildMessageWithTmpFile(mTmpFile, error_msg);
          sendReport->SetMessage(nsIMsgSendReport::process_Current, error_msg.get(), PR_FALSE);
        }
      }
      rv =  NS_MSG_UNABLE_TO_OPEN_TMP_FILE;
      goto done;
    }

    nsCOMPtr<nsIURLFetcher> fetcher = do_CreateInstance(NS_URLFETCHER_CONTRACTID, &rv);
    if (NS_FAILED(rv) || !fetcher)
    {
      if (NS_SUCCEEDED(rv))
        rv =  NS_ERROR_UNEXPECTED;
      goto done;
    }

    rv = fetcher->Initialize(mTmpFile, mOutFile, FetcherURLDoneCallback, this);
    rv = GetMessageServiceFromURI(nsDependentCString(m_uri), getter_AddRefs(messageService));
    if (NS_SUCCEEDED(rv) && messageService)
    {
      nsCAutoString uri(m_uri);
      uri += (uri.FindChar('?') == kNotFound) ? "?" : "&";
      uri.Append("fetchCompleteMessage=true");
      nsCOMPtr<nsIStreamListener> strListener;
      fetcher->QueryInterface(NS_GET_IID(nsIStreamListener), getter_AddRefs(strListener));

      // initialize a new stream converter, that uses the strListener as its input
      // obtain the input stream listener from the new converter,
      // and pass the converter's input stream listener to DisplayMessage

      m_mime_parser = do_CreateInstance(NS_MAILNEWS_MIME_STREAM_CONVERTER_CONTRACTID, &rv);
      if (NS_FAILED(rv))
        goto done;

      // Set us as the output stream for HTML data from libmime...
      nsCOMPtr<nsIMimeStreamConverter> mimeConverter = do_QueryInterface(m_mime_parser);
      if (mimeConverter)
      {
        mimeConverter->SetMimeOutputType(nsMimeOutput::nsMimeMessageDecrypt);
        mimeConverter->SetForwardInline(PR_FALSE);
        mimeConverter->SetIdentity(nsnull);
        mimeConverter->SetOriginalMsgURI(nsnull);
      }

      nsCOMPtr<nsIStreamListener> convertedListener = do_QueryInterface(m_mime_parser, &rv);
      if (NS_FAILED(rv))
        goto done;

      nsCOMPtr<nsIURI> aURL;
      rv = messageService->GetUrlForUri(uri.get(), getter_AddRefs(aURL), nsnull);
      if (aURL)
        aURL->SetSpec(nsDependentCString(uri.get()));

      rv = NS_NewInputStreamChannel(getter_AddRefs(m_converter_channel), aURL, nsnull);
      if (NS_FAILED(rv))
        goto done;

      rv = m_mime_parser->AsyncConvertData(
		    "message/rfc822",
		    "message/rfc822",
		    strListener, m_converter_channel);
      if (NS_FAILED(rv))
        goto done;

      rv = messageService->DisplayMessage(uri.get(), convertedListener, nsnull, nsnull, nsnull, nsnull);
    }
  }
done:
  if (NS_FAILED(rv))
  {
      if (mOutFile)
      {
        mOutFile->Close();
        mOutFile = nsnull;
      }

      if (mTmpFile)
      {
        mTmpFile->Remove(PR_FALSE);
        mTmpFile = nsnull;
      }
  }

  return rv;
}

#ifdef XP_MACOSX
PRBool nsMsgAttachmentHandler::HasResourceFork(FSSpec *fsSpec)
{
  FSRef fsRef;
  if (::FSpMakeFSRef(fsSpec, &fsRef) == noErr)
  {
    FSCatalogInfo catalogInfo;
    OSErr err = ::FSGetCatalogInfo(&fsRef, kFSCatInfoDataSizes + kFSCatInfoRsrcSizes, &catalogInfo, nsnull, nsnull, nsnull);
    return (err == noErr && catalogInfo.rsrcLogicalSize != 0);
  }
  return PR_FALSE;
}
#endif

nsresult
nsMsgAttachmentHandler::SnarfAttachment(nsMsgCompFields *compFields)
{
  NS_ASSERTION (! m_done, "Already done");

  if (!mURL)
    return SnarfMsgAttachment(compFields);

  mCompFields = compFields;

  // First, get as file spec and create the stream for the
  // temp file where we will save this data
  nsCOMPtr <nsIFile> tmpFile;
  nsresult rv = nsMsgCreateTempFile("nsmail.tmp", getter_AddRefs(tmpFile));
  NS_ENSURE_SUCCESS(rv, rv);
  mTmpFile = do_QueryInterface(tmpFile);
  mDeleteFile = PR_TRUE;

  rv = MsgNewBufferedFileOutputStream(getter_AddRefs(mOutFile), mTmpFile, -1, 00600);
  if (NS_FAILED(rv) || !mOutFile)
  {
    if (m_mime_delivery_state)
    {
      nsCOMPtr<nsIMsgSendReport> sendReport;
      m_mime_delivery_state->GetSendReport(getter_AddRefs(sendReport));
      if (sendReport)
      {
        nsAutoString error_msg;
        nsMsgBuildMessageWithTmpFile(mTmpFile, error_msg);
        sendReport->SetMessage(nsIMsgSendReport::process_Current, error_msg.get(), PR_FALSE);
      }
    }
    mTmpFile->Remove(PR_FALSE);
    mTmpFile = nsnull;
    return NS_MSG_UNABLE_TO_OPEN_TMP_FILE;
  }

  nsCString sourceURISpec;
  mURL->GetSpec(sourceURISpec);
#ifdef XP_MACOSX
  if (!m_bogus_attachment && StringBeginsWith(sourceURISpec, NS_LITERAL_CSTRING("file://")))
  {
    // Unescape the path (i.e. un-URLify it) before making a FSSpec
    nsCAutoString filePath;
    filePath.Adopt(nsMsgGetLocalFileFromURL(sourceURISpec.get()));
    nsUnescape(filePath.BeginWriting());

    nsCOMPtr<nsILocalFile> sourceFile;
    NS_NewNativeLocalFile(filePath, PR_TRUE, getter_AddRefs(sourceFile));
    if (!sourceFile)
      return NS_ERROR_FAILURE;
      
    // check if it is a bundle. if it is, we'll zip it. 
    // if not, we'll apple encode it (applesingle or appledouble)
    nsCOMPtr<nsILocalFileMac> macFile(do_QueryInterface(sourceFile));
    PRBool isPackage;
    macFile->IsPackage(&isPackage);
    if (isPackage)
      rv = ConvertToZipFile(macFile);
    else
      rv = ConvertToAppleEncoding(sourceURISpec, filePath, macFile);
    
    NS_ENSURE_SUCCESS(rv, rv);
  }
#endif /* XP_MACOSX */

  //
  // Ok, here we are, we need to fire the URL off and get the data
  // in the temp file
  //
  // Create a fetcher for the URL attachment...
  
  nsCOMPtr<nsIURLFetcher> fetcher = do_CreateInstance(NS_URLFETCHER_CONTRACTID, &rv);
  if (NS_FAILED(rv) || !fetcher)
  {
    if (NS_SUCCEEDED(rv))
      return NS_ERROR_UNEXPECTED;
    else
      return rv;
  }

  return fetcher->FireURLRequest(mURL, mTmpFile, mOutFile, FetcherURLDoneCallback, this);
}

#ifdef XP_MACOSX
nsresult
nsMsgAttachmentHandler::ConvertToZipFile(nsILocalFileMac *aSourceFile)
{
  // append ".zip" to the real file name
  nsCAutoString zippedName;
  nsresult rv = aSourceFile->GetNativeLeafName(zippedName);
  NS_ENSURE_SUCCESS(rv, rv);
  zippedName.AppendLiteral(".zip");

  // create a temporary file that we'll work on
  nsCOMPtr <nsIFile> tmpFile;
  rv = nsMsgCreateTempFile(zippedName.get(), getter_AddRefs(tmpFile));
  NS_ENSURE_SUCCESS(rv, rv);
  mEncodedWorkingFile = do_QueryInterface(tmpFile);
  
  // point our URL at the zipped temp file
  NS_NewFileURI(getter_AddRefs(mURL), mEncodedWorkingFile);
  
  // zip it!
  rv = nsSimpleZipper::Zip(aSourceFile, mEncodedWorkingFile);
  NS_ENSURE_SUCCESS(rv, rv);
  
  // set some metadata for this attachment, that will affect the MIME headers.
  PR_Free(m_type);
  m_type = PL_strdup(APPLICATION_ZIP);
  PR_Free(m_real_name);
  m_real_name = PL_strdup(zippedName.get());
  
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::ConvertToAppleEncoding(const nsCString &aFileURI, 
                                               const nsCString &aFilePath, 
                                               nsILocalFileMac *aSourceFile)
{
  // convert the apple file to AppleDouble first, and then patch the
  // address in the url.
  

  //We need to retrieve the file type and creator...
  FSSpec fsSpec;
  aSourceFile->GetFSSpec(&fsSpec);
  FInfo info;
  if (FSpGetFInfo (&fsSpec, &info) == noErr)
  {
    char filetype[32];
    PR_snprintf(filetype, sizeof(filetype), "%X", info.fdType);
    PR_Free(m_x_mac_type);
    m_x_mac_type = PL_strdup(filetype);

    PR_snprintf(filetype, sizeof(filetype), "%X", info.fdCreator);
    PR_Free(m_x_mac_creator);
    m_x_mac_creator = PL_strdup(filetype);
  }

  PRBool sendResourceFork = HasResourceFork(&fsSpec);

  // if we have a resource fork, check the filename extension, maybe we don't need the resource fork!
  if (sendResourceFork)
  {
    nsCOMPtr<nsIURL> fileUrl(do_CreateInstance(NS_STANDARDURL_CONTRACTID));
    if (fileUrl)
    {
      nsresult rv = fileUrl->SetSpec(aFileURI);
      if (NS_SUCCEEDED(rv))
      {
        nsCAutoString ext;
        rv = fileUrl->GetFileExtension(ext);
        if (NS_SUCCEEDED(rv) && !ext.IsEmpty())
        {
          sendResourceFork =
          PL_strcasecmp(ext.get(), "TXT") &&
          PL_strcasecmp(ext.get(), "JPG") &&
          PL_strcasecmp(ext.get(), "GIF") &&
          PL_strcasecmp(ext.get(), "TIF") &&
          PL_strcasecmp(ext.get(), "HTM") &&
          PL_strcasecmp(ext.get(), "HTML") &&
          PL_strcasecmp(ext.get(), "ART") &&
          PL_strcasecmp(ext.get(), "XUL") &&
          PL_strcasecmp(ext.get(), "XML") &&
          PL_strcasecmp(ext.get(), "CSS") &&
          PL_strcasecmp(ext.get(), "JS");
        }
      }
    }
  }

  // Only use appledouble if we aren't uuencoding.
  if( sendResourceFork )
  {
    char *separator;

    separator = mime_make_separator("ad");
    if (!separator)
      return NS_ERROR_OUT_OF_MEMORY;

    nsCOMPtr <nsIFile> tmpFile;
    nsresult rv = nsMsgCreateTempFile("appledouble", getter_AddRefs(tmpFile));
    if (NS_SUCCEEDED(rv))
      mEncodedWorkingFile = do_QueryInterface(tmpFile);
    if (!mEncodedWorkingFile)
    {
      PR_FREEIF(separator);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    //
    // RICHIE_MAC - ok, here's the deal, we have a file that we need
    // to encode in appledouble encoding for the resource fork and put that
    // into the mEncodedWorkingFile location. Then, we need to patch the new file
    // spec into the array and send this as part of the 2 part appledouble/mime
    // encoded mime part.
    //
    AppleDoubleEncodeObject     *obj = new (AppleDoubleEncodeObject);
    if (obj == NULL)
    {
      mEncodedWorkingFile = nsnull;
      PR_FREEIF(separator);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    rv = MsgGetFileStream(mEncodedWorkingFile, getter_AddRefs(obj->fileStream));
    if (NS_FAILED(rv) || !obj->fileStream)
    {
      PR_FREEIF(separator);
      delete obj;
      return NS_ERROR_OUT_OF_MEMORY;
    }

    PRInt32     bSize = AD_WORKING_BUFF_SIZE;

    char  *working_buff = nsnull;
    while (!working_buff && (bSize >= 512))
    {
      working_buff = (char *)PR_CALLOC(bSize);
      if (!working_buff)
        bSize /= 2;
    }

    if (!working_buff)
    {
      PR_FREEIF(separator);
      delete obj;
      return NS_ERROR_OUT_OF_MEMORY;
    }

    obj->buff = working_buff;
    obj->s_buff = bSize;

    //
    //  Setup all the need information on the apple double encoder.
    //
    ap_encode_init(&(obj->ap_encode_obj), aFilePath.get(), separator);

    PRInt32 count;

    nsresult status = noErr;
    m_size = 0;
    while (status == noErr)
    {
      status = ap_encode_next(&(obj->ap_encode_obj), obj->buff, bSize, &count);
      if (status == noErr || status == errDone)
      {
        //
        // we got the encode data, so call the next stream to write it to the disk.
        //
        PRUint32 bytesWritten;
        obj->fileStream->Write(obj->buff, count, &bytesWritten);
        if (bytesWritten != (PRUint32) count)
          status = NS_MSG_ERROR_WRITING_FILE;
      }
    }

    ap_encode_end(&(obj->ap_encode_obj), (status >= 0)); // if this is true, ok, false abort
    if (obj->fileStream)
      obj->fileStream->Close();

    PR_FREEIF(obj->buff);               /* free the working buff.   */
    PR_FREEIF(obj);

    nsCOMPtr <nsIURI> fileURI;
    NS_NewFileURI(getter_AddRefs(fileURI), mEncodedWorkingFile);

    nsCOMPtr<nsIFileURL> theFileURL = do_QueryInterface(fileURI, &rv);
    NS_ENSURE_SUCCESS(rv,rv);

    nsCString newURLSpec;
    NS_ENSURE_SUCCESS(rv, rv);
    fileURI->GetSpec(newURLSpec);

    if (newURLSpec.IsEmpty())
    {
      PR_FREEIF(separator);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (NS_FAILED(nsMsgNewURL(getter_AddRefs(mURL), newURLSpec.get())))
    {
      PR_FREEIF(separator);
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Now after conversion, also patch the types.
    char        tmp[128];
    PR_snprintf(tmp, sizeof(tmp), MULTIPART_APPLEDOUBLE ";\r\n boundary=\"%s\"", separator);
    PR_FREEIF(separator);
    PR_Free(m_type);
    m_type = PL_strdup(tmp);
  }
  else
  {
    if ( sendResourceFork )
    {
      // For now, just do the encoding, but in the old world we would ask the
      // user about doing this conversion
      printf("...we could ask the user about this conversion, but for now, nahh..\n");
    }

    PRBool    useDefault;
    char      *macType, *macEncoding;
    if (m_type == NULL || !PL_strcasecmp (m_type, TEXT_PLAIN))
    {
# define TEXT_TYPE  0x54455854  /* the characters 'T' 'E' 'X' 'T' */
# define text_TYPE  0x74657874  /* the characters 't' 'e' 'x' 't' */

      if (info.fdType != TEXT_TYPE && info.fdType != text_TYPE)
      {
        MacGetFileType(aSourceFile, &useDefault, &macType, &macEncoding);
        PR_Free(m_type);
        m_type = macType;
      }
    }
    // don't bother to set the types if we failed in getting the file info.
  }

  return NS_OK;
}
#endif // XP_MACOSX

nsresult
nsMsgAttachmentHandler::LoadDataFromFile(nsILocalFile *file, nsString &sigData, PRBool charsetConversion)
{
  PRInt32       readSize;
  char          *readBuf;

  nsCOMPtr <nsIInputStream> inputFile;
  nsresult rv = NS_NewLocalFileInputStream(getter_AddRefs(inputFile), file);
  if (NS_FAILED(rv))
    return NS_MSG_ERROR_WRITING_FILE;

  PRInt64 fileSize;
  file->GetFileSize(&fileSize);
  readSize = (PRUint32) fileSize;

  readBuf = (char *)PR_Malloc(readSize + 1);
  if (!readBuf)
    return NS_ERROR_OUT_OF_MEMORY;
  memset(readBuf, 0, readSize + 1);

  PRUint32 bytesRead;
  inputFile->Read(readBuf, readSize, &bytesRead);
  inputFile->Close();

  if (charsetConversion)
  {
    if (NS_FAILED(ConvertToUnicode(m_charset, nsDependentCString(readBuf), sigData)))
      CopyASCIItoUTF16(readBuf, sigData);
  }
  else
    CopyASCIItoUTF16(readBuf, sigData);

  PR_FREEIF(readBuf);
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::Abort()
{
  NS_ASSERTION(m_mime_delivery_state != nsnull, "not-null m_mime_delivery_state");

  if (m_done)
    return NS_OK;

  if (mRequest)
    return mRequest->Cancel(NS_ERROR_ABORT);
  else
    if (m_mime_delivery_state)
    {
      m_mime_delivery_state->SetStatus(NS_ERROR_ABORT);
      m_mime_delivery_state->NotifyListenerOnStopSending(nsnull, NS_ERROR_ABORT, 0, nsnull);
    }

  return NS_OK;

}

nsresult
nsMsgAttachmentHandler::UrlExit(nsresult status, const PRUnichar* aMsg)
{
  NS_ASSERTION(m_mime_delivery_state != nsnull, "not-null m_mime_delivery_state");

  // Close the file, but don't delete the disk file (or the file spec.)
  if (mOutFile)
  {
    mOutFile->Close();
    mOutFile = nsnull;
  }
  // this silliness is because Windows nsILocalFile caches its file size
  // so if an output stream writes to it, it will still return the original
  // cached size.
  if (mTmpFile)
  {
    nsCOMPtr <nsIFile> tmpFile;
    mTmpFile->Clone(getter_AddRefs(tmpFile));
    mTmpFile = do_QueryInterface(tmpFile);
  }
  mRequest = nsnull;

  // First things first, we are now going to see if this is an HTML
  // Doc and if it is, we need to see if we can determine the charset
  // for this part by sniffing the HTML file.
  // This is needed only when the charset is not set already.
  // (e.g. a charset may be specified in HTTP header)
  //
  if ( (m_type) &&  (*m_type) &&
       (!m_charset || !(*m_charset)) )
  {
    if (PL_strcasecmp(m_type, TEXT_HTML) == 0)
    {
      char *tmpCharset = (char *)nsMsgI18NParseMetaCharset(mTmpFile);
      if (tmpCharset[0] != '\0')
      {
        PR_Free(m_charset);
        m_charset = PL_strdup(tmpCharset);
      }
    }
  }

  nsresult mimeDeliveryStatus;
  m_mime_delivery_state->GetStatus(&mimeDeliveryStatus);

  if (mimeDeliveryStatus == NS_ERROR_ABORT)
    status = NS_ERROR_ABORT;

  if (NS_FAILED(status) && status != NS_ERROR_ABORT && NS_SUCCEEDED(mimeDeliveryStatus))
  {
    // At this point, we should probably ask a question to the user
    // if we should continue without this attachment.
    //
    PRBool            keepOnGoing = PR_TRUE;
    nsCString    turl;
    nsString     msg;
    PRUnichar         *printfString = nsnull;
    nsresult rv;
    nsCOMPtr<nsIStringBundleService> bundleService(do_GetService("@mozilla.org/intl/stringbundle;1", &rv));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIStringBundle> bundle;
    rv = bundleService->CreateBundle("chrome://messenger/locale/messengercompose/composeMsgs.properties", getter_AddRefs(bundle));
    NS_ENSURE_SUCCESS(rv, rv);
    nsMsgDeliverMode mode = nsIMsgSend::nsMsgDeliverNow;
    m_mime_delivery_state->GetDeliveryMode(&mode);
    if (mode == nsIMsgSend::nsMsgSaveAsDraft || mode == nsIMsgSend::nsMsgSaveAsTemplate)
      bundle->GetStringFromID(NS_MSG_FAILURE_ON_OBJ_EMBED_WHILE_SAVING, getter_Copies(msg));
    else
      bundle->GetStringFromID(NS_MSG_FAILURE_ON_OBJ_EMBED_WHILE_SENDING, getter_Copies(msg));
    if (m_real_name && *m_real_name)
      printfString = nsTextFormatter::smprintf(msg.get(), m_real_name);
    else
    if (NS_SUCCEEDED(mURL->GetSpec(turl)) && !turl.IsEmpty())
      {
        nsCAutoString unescapeUrl(turl);
        nsUnescape(unescapeUrl.BeginWriting());
        if (unescapeUrl.IsEmpty())
          printfString = nsTextFormatter::smprintf(msg.get(), turl.get());
        else
          printfString = nsTextFormatter::smprintf(msg.get(), unescapeUrl.get());
      }
    else
      printfString = nsTextFormatter::smprintf(msg.get(), "?");

    nsCOMPtr<nsIPrompt> aPrompt;
    if (m_mime_delivery_state)
      m_mime_delivery_state->GetDefaultPrompt(getter_AddRefs(aPrompt));
    nsMsgAskBooleanQuestionByString(aPrompt, printfString, &keepOnGoing);
    PR_FREEIF(printfString);

    if (keepOnGoing)
    {
      status = 0;
      m_bogus_attachment = PR_TRUE; //That will cause this attachment to be ignored.
    }
    else
    {
      status = NS_ERROR_ABORT;
      m_mime_delivery_state->SetStatus(status);
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, nsnull, &ignoreMe);
      m_mime_delivery_state->NotifyListenerOnStopSending(nsnull, status, 0, nsnull);
      SetMimeDeliveryState(nsnull);
      return status;
    }
  }

  m_done = PR_TRUE;

  //
  // Ok, now that we have the file here on disk, we need to see if there was
  // a need to do conversion to plain text...if so, the magic happens here,
  // otherwise, just move on to other attachments...
  //
  if (NS_SUCCEEDED(status) && m_type && PL_strcasecmp(m_type, TEXT_PLAIN) )
  {
    if (m_desired_type && !PL_strcasecmp(m_desired_type, TEXT_PLAIN) )
    {
      //
      // Conversion to plain text desired.
      //
      PRInt32       width = 72;
      nsCOMPtr<nsIPrefBranch> pPrefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));
      if (pPrefBranch)
        pPrefBranch->GetIntPref("mailnews.wraplength", &width);
      // Let sanity reign!
      if (width == 0)
        width = 72;
      else if (width < 10)
        width = 10;
      else if (width > 30000)
        width = 30000;

      //
      // Now use the converter service here to do the right
      // thing and convert this data to plain text for us!
      //
      nsAutoString      conData;

      if (NS_SUCCEEDED(LoadDataFromFile(mTmpFile, conData, PR_TRUE)))
      {
        if (NS_SUCCEEDED(ConvertBufToPlainText(conData, UseFormatFlowed(m_charset))))
        {
          if (mDeleteFile)
            mTmpFile->Remove(PR_FALSE);

          nsCOMPtr<nsIOutputStream> outputStream;
          nsresult rv = NS_NewLocalFileOutputStream(getter_AddRefs(outputStream), mTmpFile,  PR_WRONLY | PR_CREATE_FILE, 00600);

          if (NS_SUCCEEDED(rv))
          {
            nsCAutoString tData;
            if (NS_FAILED(ConvertFromUnicode(m_charset, conData, tData)))
              LossyCopyUTF16toASCII(conData, tData);
            if (!tData.IsEmpty())
            {
              PRUint32 bytesWritten;
              (void) outputStream->Write(tData.get(), tData.Length(), &bytesWritten);
            }
            outputStream->Close();
            // this silliness is because Windows nsILocalFile caches its file size
            // so if an output stream writes to it, it will still return the original
            // cached size.
            if (mTmpFile)
            {
              nsCOMPtr <nsIFile> tmpFile;
              mTmpFile->Clone(getter_AddRefs(tmpFile));
              mTmpFile = do_QueryInterface(tmpFile);
            }

          }
        }
      }

      PR_FREEIF(m_type);
      m_type = m_desired_type;
      m_desired_type = nsnull;
      PR_FREEIF(m_encoding);
      m_encoding = nsnull;
    }
  }

  PRUint32 pendingAttachmentCount = 0;
  m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
  NS_ASSERTION (pendingAttachmentCount > 0, "no more pending attachment");

  m_mime_delivery_state->SetPendingAttachmentCount(pendingAttachmentCount - 1);

  PRBool processAttachmentsSynchronously = PR_FALSE;
  m_mime_delivery_state->GetProcessAttachmentsSynchronously(&processAttachmentsSynchronously);
  if (NS_SUCCEEDED(status) && processAttachmentsSynchronously)
  {
    /* Find the next attachment which has not yet been loaded,
     if any, and start it going.
     */
    PRUint32 i;
    nsMsgAttachmentHandler *next = 0;
    nsMsgAttachmentHandler *attachments = nsnull;
    PRUint32 attachmentCount = 0;

    m_mime_delivery_state->GetAttachmentCount(&attachmentCount);
    if (attachmentCount)
      m_mime_delivery_state->GetAttachmentHandlers(&attachments);

    for (i = 0; i < attachmentCount; i++)
    {
      if (!attachments[i].m_done)
      {
        next = &attachments[i];
        //
        // rhp: We need to get a little more understanding to failed URL
        // requests. So, at this point if most of next is NULL, then we
        // should just mark it fetched and move on! We probably ignored
        // this earlier on in the send process.
        //
        if ( (!next->mURL) && (!next->m_uri) )
        {
          attachments[i].m_done = PR_TRUE;
          m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
          m_mime_delivery_state->SetPendingAttachmentCount(pendingAttachmentCount - 1);
          next->mPartUserOmissionOverride = PR_TRUE;
          next = nsnull;
          continue;
        }

        break;
      }
    }

    if (next)
    {
      int status = next->SnarfAttachment(mCompFields);
      if (NS_FAILED(status))
      {
        nsresult ignoreMe;
        m_mime_delivery_state->Fail(status, nsnull, &ignoreMe);
        m_mime_delivery_state->NotifyListenerOnStopSending(nsnull, status, 0, nsnull);
        SetMimeDeliveryState(nsnull);
        return NS_ERROR_UNEXPECTED;
      }
    }
  }

  m_mime_delivery_state->GetPendingAttachmentCount(&pendingAttachmentCount);
  if (pendingAttachmentCount == 0)
  {
    // If this is the last attachment, then either complete the
    // delivery (if successful) or report the error by calling
    // the exit routine and terminating the delivery.
    if (NS_FAILED(status))
    {
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
      m_mime_delivery_state->NotifyListenerOnStopSending(nsnull, status, aMsg, nsnull);
      SetMimeDeliveryState(nsnull);
      return NS_ERROR_UNEXPECTED;
    }
    else
    {
      status = m_mime_delivery_state->GatherMimeAttachments ();
      if (NS_FAILED(status))
      {
        nsresult ignoreMe;
        m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
        m_mime_delivery_state->NotifyListenerOnStopSending(nsnull, status, aMsg, nsnull);
        SetMimeDeliveryState(nsnull);
        return NS_ERROR_UNEXPECTED;
      }
    }
  }
  else
  {
    // If this is not the last attachment, but it got an error,
    // then report that error and continue
    if (NS_FAILED(status))
    {
      nsresult ignoreMe;
      m_mime_delivery_state->Fail(status, aMsg, &ignoreMe);
    }
  }

  SetMimeDeliveryState(nsnull);
  return NS_OK;
}


nsresult
nsMsgAttachmentHandler::GetMimeDeliveryState(nsIMsgSend** _retval)
{
  NS_ENSURE_ARG(_retval);
  *_retval = m_mime_delivery_state;
  NS_IF_ADDREF(*_retval);
  return NS_OK;
}

nsresult
nsMsgAttachmentHandler::SetMimeDeliveryState(nsIMsgSend* mime_delivery_state)
{
  /*
    Because setting m_mime_delivery_state to null could destroy ourself as
    m_mime_delivery_state it's our parent, we need to protect ourself against
    that!

    This extra comptr is necessary,
    see bug http://bugzilla.mozilla.org/show_bug.cgi?id=78967
  */
  nsCOMPtr<nsIMsgSend> temp = m_mime_delivery_state; /* Should lock our parent until the end of the function */
  m_mime_delivery_state = mime_delivery_state;
  return NS_OK;
}
