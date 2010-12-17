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
 * Portions created by the Initial Developer are Copyright (C) 1999
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

#include "msgCore.h"
#include "nsMailDatabase.h"
#include "nsDBFolderInfo.h"
#include "nsMsgLocalFolderHdrs.h"
#include "nsNetUtil.h"
#include "nsISeekableStream.h"
#include "nsMsgOfflineImapOperation.h"
#include "nsMsgFolderFlags.h"
#include "prlog.h"
#include "prprf.h"
#include "nsMsgUtils.h"
#ifdef PUTUP_ALERT_ON_INVALID_DB
#include "nsIPrompt.h"
#include "nsIWindowWatcher.h"
#endif

extern PRLogModuleInfo *IMAPOffline;

const char *kOfflineOpsScope = "ns:msg:db:row:scope:ops:all";	// scope for all offine ops table
const char *kOfflineOpsTableKind = "ns:msg:db:table:kind:ops";
struct mdbOid gAllOfflineOpsTableOID;


nsMailDatabase::nsMailDatabase()
    : m_reparse(PR_FALSE), m_ownFolderStream(PR_FALSE)
{
  m_mdbAllOfflineOpsTable = nsnull;
}

nsMailDatabase::~nsMailDatabase()
{
}

NS_IMETHODIMP nsMailDatabase::SetFolderStream(nsIOutputStream *aFileStream)
{
  NS_ASSERTION(!m_folderStream || !aFileStream, "m_folderStream is not null and we are assigning a non null stream to it");
  m_folderStream = aFileStream; //m_folderStream is set externally, so m_ownFolderStream is false
  m_ownFolderStream = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMailDatabase::GetFolderStream(nsIOutputStream **aFileStream)
{
  NS_ENSURE_ARG_POINTER(aFileStream);
  if (!m_folderStream)
  {
    nsresult rv = MsgGetFileStream(m_folderFile, getter_AddRefs(m_folderStream));
    NS_ENSURE_SUCCESS(rv, rv);
    m_ownFolderStream = PR_TRUE;
  }
  
  NS_IF_ADDREF(*aFileStream = m_folderStream);
  return NS_OK;
}

static PRBool gGotGlobalPrefs = PR_FALSE;
static PRInt32 gTimeStampLeeway;

void nsMailDatabase::GetGlobalPrefs()
{
  if (!gGotGlobalPrefs)
  {
    nsMsgDatabase::GetGlobalPrefs();
    GetIntPref("mail.db_timestamp_leeway", &gTimeStampLeeway);
    gGotGlobalPrefs = PR_TRUE;
  }
}
// caller passes in upgrading==PR_TRUE if they want back a db even if the db is out of date.
// If so, they'll extract out the interesting info from the db, close it, delete it, and
// then try to open the db again, prior to reparsing.
NS_IMETHODIMP nsMailDatabase::Open(nsILocalFile *aFolderName, PRBool aCreate, PRBool aUpgrading)
{
  m_folderFile = aFolderName;
  nsresult rv = nsMsgDatabase::Open(aFolderName, aCreate, aUpgrading);
  return rv;
}

NS_IMETHODIMP nsMailDatabase::ForceClosed()
{
  m_mdbAllOfflineOpsTable = nsnull;
  return nsMsgDatabase::ForceClosed();
}

// get this on demand so that only db's that have offline ops will
// create the table.	
nsresult nsMailDatabase::GetAllOfflineOpsTable()
{
  nsresult rv = NS_OK;
  if (!m_mdbAllOfflineOpsTable)
    rv = GetTableCreateIfMissing(kOfflineOpsScope, kOfflineOpsTableKind, getter_AddRefs(m_mdbAllOfflineOpsTable), 
                                                m_offlineOpsRowScopeToken, m_offlineOpsTableKindToken) ;
  return rv;
}

// cache m_folderStream to make updating mozilla status flags fast
NS_IMETHODIMP nsMailDatabase::StartBatch()
{
  if (!m_folderStream && m_folder)  //only if we create a stream, set m_ownFolderStream to true.
  {
    PRBool isLocked;
    m_folder->GetLocked(&isLocked);
    if (isLocked)
    {
      NS_ASSERTION(PR_FALSE, "Some other operation is in progress");
      return NS_MSG_FOLDER_BUSY;
    }
    nsresult rv = MsgGetFileStream(m_folderFile, getter_AddRefs(m_folderStream));
    NS_ENSURE_SUCCESS(rv, rv);
    m_ownFolderStream = PR_TRUE;
  }

  return NS_OK;
}

NS_IMETHODIMP nsMailDatabase::EndBatch()
{
  if (m_ownFolderStream)   //only if we own the stream, then we should close it
  {
    if (m_folderStream)
    {
      m_folderStream->Flush();
      m_folderStream->Close();  
    }
    m_folderStream = nsnull;
    m_ownFolderStream = PR_FALSE;
  }
  SetSummaryValid(PR_TRUE);
  return NS_OK;
}

NS_IMETHODIMP nsMailDatabase::DeleteMessages(PRUint32 aNumKeys, nsMsgKey* nsMsgKeys, nsIDBChangeListener *instigator)
{
  nsresult rv;
  if (!m_folderStream && m_folder)
  {
    PRBool isLocked;
    m_folder->GetLocked(&isLocked);
    if (isLocked)
    {
      NS_ASSERTION(PR_FALSE, "Some other operation is in progress");
      return NS_MSG_FOLDER_BUSY;
    }
    rv = MsgGetFileStream(m_folderFile, getter_AddRefs(m_folderStream));
    NS_ENSURE_SUCCESS(rv, rv);
    m_ownFolderStream = PR_TRUE;
  }

  rv = nsMsgDatabase::DeleteMessages(aNumKeys, nsMsgKeys, instigator);
  if (m_ownFolderStream)//only if we own the stream, then we should close it
  {
    if (m_folderStream)
    {
      m_folderStream->Flush(); // this does a sync
      m_folderStream->Close();
    }
    m_folderStream = nsnull;
    m_ownFolderStream = PR_FALSE;
  }

  SetFolderInfoValid(m_folderFile, 0, 0);
  return rv;
}

// Helper routine - lowest level of flag setting
PRBool nsMailDatabase::SetHdrFlag(nsIMsgDBHdr *msgHdr, PRBool bSet, nsMsgMessageFlagType flag)
{
  nsIOutputStream *fileStream = nsnull;
  PRBool ret = PR_FALSE;

  if (!m_folderStream && m_folder)  //we are going to create a stream, bail out if someone else has lock
  {
    PRBool isLocked;
    m_folder->GetLocked(&isLocked);
    if (isLocked)
    {
      NS_ASSERTION(PR_FALSE, "Some other operation is in progress");
      return PR_FALSE;
    }
  }
  if (nsMsgDatabase::SetHdrFlag(msgHdr, bSet, flag))
  {
    UpdateFolderFlag(msgHdr, bSet, flag, &fileStream);
    if (fileStream)
    {
      fileStream->Flush();
      fileStream->Close();
      NS_RELEASE(fileStream);
      SetFolderInfoValid(m_folderFile, 0, 0);
    }
    ret = PR_TRUE;
  }

  return ret;
}

// ### should move this into some utils class...
int msg_UnHex(char C)
{
	return ((C >= '0' && C <= '9') ? C - '0' :
			((C >= 'A' && C <= 'F') ? C - 'A' + 10 :
			 ((C >= 'a' && C <= 'f') ? C - 'a' + 10 : 0)));
}


// We let the caller close the file in case he's updating a lot of flags
// and we don't want to open and close the file every time through.
// As an experiment, try caching the fid in the db as m_folderFile.
// If this is set, use it but don't return *pFid.
void nsMailDatabase::UpdateFolderFlag(nsIMsgDBHdr *mailHdr, PRBool bSet, 
                                      nsMsgMessageFlagType flag,
                                      nsIOutputStream **ppFileStream)
{
  static char buf[50];
  PRInt64 folderStreamPos = 0; //saves the folderStream pos in case we are sharing the stream with other code
  nsIOutputStream *fileStream = (m_folderStream) ? m_folderStream.get() : *ppFileStream;
  PRUint32 offset;
  (void)mailHdr->GetStatusOffset(&offset);
  nsCOMPtr <nsISeekableStream> seekableStream;
  
  nsresult rv;
  
  if (offset > 0) 
  {
    
    if (fileStream == NULL) 
    {
      rv = MsgGetFileStream(m_folderFile, &fileStream);
      if (NS_FAILED(rv))
        return;
      seekableStream = do_QueryInterface(fileStream);
    }
    else if (!m_ownFolderStream)
    {
      m_folderStream->Flush();
      seekableStream = do_QueryInterface(fileStream);
      seekableStream->Tell(&folderStreamPos);
    }
    else
      seekableStream = do_QueryInterface(m_folderStream);
      
    if (fileStream) 
    {
      PRUint64 msgOffset;
      (void)mailHdr->GetMessageOffset(&msgOffset);
      PRUint64 statusPos = msgOffset + offset;
      NS_ASSERTION(offset < 10000, "extremely unlikely status offset");
      seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, statusPos);
      buf[0] = '\0';
      nsCOMPtr <nsIInputStream> inputStream = do_QueryInterface(fileStream);
      PRUint32 bytesRead;
      if (NS_SUCCEEDED(inputStream->Read(buf, X_MOZILLA_STATUS_LEN + 6, &bytesRead)))
      {
        buf[bytesRead] = '\0';
        if (strncmp(buf, X_MOZILLA_STATUS, X_MOZILLA_STATUS_LEN) == 0 &&
          strncmp(buf + X_MOZILLA_STATUS_LEN, ": ", 2) == 0 &&
          strlen(buf) >= X_MOZILLA_STATUS_LEN + 6) 
        {
          PRUint32 flags;
          PRUint32 bytesWritten;
          (void)mailHdr->GetFlags(&flags);
          if (!(flags & nsMsgMessageFlags::Expunged))
          {
            int i;
            char *p = buf + X_MOZILLA_STATUS_LEN + 2;
            
            for (i=0, flags = 0; i<4; i++, p++)
            {
              flags = (flags << 4) | msg_UnHex(*p);
            }
            
            PRUint32 curFlags;
            (void)mailHdr->GetFlags(&curFlags);
            flags = (flags & nsMsgMessageFlags::Queued) |
              (curFlags & ~nsMsgMessageFlags::RuntimeOnly);
          }
          else
          {
            flags &= ~nsMsgMessageFlags::RuntimeOnly;
          }
          seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, statusPos);
          // We are filing out x-mozilla-status flags here
          PR_snprintf(buf, sizeof(buf), X_MOZILLA_STATUS_FORMAT,
            flags & 0x0000FFFF);
          PRInt32 lineLen = PL_strlen(buf);
          PRUint64 status2Pos = statusPos + lineLen + MSG_LINEBREAK_LEN;
          fileStream->Write(buf, lineLen, &bytesWritten);
          
          // time to upate x-mozilla-status2
          seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, status2Pos);
          if (NS_SUCCEEDED(inputStream->Read(buf, X_MOZILLA_STATUS2_LEN + 10, &bytesRead)))
          {
            if (strncmp(buf, X_MOZILLA_STATUS2, X_MOZILLA_STATUS2_LEN) == 0 &&
              strncmp(buf + X_MOZILLA_STATUS2_LEN, ": ", 2) == 0 &&
              strlen(buf) >= X_MOZILLA_STATUS2_LEN + 10) 
            {
              PRUint32 dbFlags;
              (void)mailHdr->GetFlags(&dbFlags);
              dbFlags &= 0xFFFF0000;
              seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, status2Pos);
              PR_snprintf(buf, sizeof(buf), X_MOZILLA_STATUS2_FORMAT, dbFlags);
              fileStream->Write(buf, PL_strlen(buf), &bytesWritten);
            }
          }
        } else 
        {
#ifdef DEBUG
          printf("Didn't find %s where expected at position %ld\n"
            "instead, found %s.\n",
            X_MOZILLA_STATUS, (long) statusPos, buf);
#endif
          SetReparse(PR_TRUE);
        }			
      } 
      else 
      {
#ifdef DEBUG
        printf("Couldn't read old status line at all at position %ld\n",
          (long) statusPos);
#endif
        SetReparse(PR_TRUE);
      }
    }
    else
    {
#ifdef DEBUG
      nsCString folderPath;
      m_folderFile->GetNativePath(folderPath);
      printf("Couldn't open mail folder for update%s!\n",
        folderPath.get());
#endif
      PR_ASSERT(PR_FALSE);
    }
    if (!m_folderStream)
      *ppFileStream = fileStream; // This tells the caller that we opened the file, and please to close it.
    else if (!m_ownFolderStream)
      seekableStream->Seek(nsISeekableStream::NS_SEEK_SET, folderStreamPos);
  }
}

// Get the current attributes of the mbox file, corrected for caching
void nsMailDatabase::GetMailboxModProperties(PRInt64 *aSize, PRUint32 *aDate)
{
  // We'll simply return 0 on errors.
  *aDate = 0;
  *aSize = 0;
  if (!m_folderFile)
    return;

  // clone file because nsLocalFile caches sizes and dates.
  nsCOMPtr<nsIFile> copyFolderFile;
  nsresult rv = m_folderFile->Clone(getter_AddRefs(copyFolderFile));
  if (NS_FAILED(rv) || !copyFolderFile)
    return;

  rv = copyFolderFile->GetFileSize(aSize);
  if (NS_FAILED(rv))
    return;

  PRInt64 lastModTime;
  rv = copyFolderFile->GetLastModifiedTime(&lastModTime);
  if (NS_FAILED(rv))
    return;

  PRTime  temp64;
  PRInt64 thousand;
  LL_I2L(thousand, PR_MSEC_PER_SEC);
  LL_DIV(temp64, lastModTime, thousand);
  LL_L2UI(*aDate, temp64);
  return;
}

NS_IMETHODIMP nsMailDatabase::GetSummaryValid(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  PRUint64 folderSize;
  PRUint32  folderDate;
  PRInt32 numUnreadMessages;
  nsAutoString errorMsg;

  *aResult = PR_FALSE;

  if (m_folderFile && m_dbFolderInfo)
  {
    m_dbFolderInfo->GetNumUnreadMessages(&numUnreadMessages);
    m_dbFolderInfo->GetFolderSize64(&folderSize);
    m_dbFolderInfo->GetFolderDate(&folderDate);

    // compare current version of db versus filed out version info, 
    // and file size in db vs file size on disk.
    PRUint32 version;
    m_dbFolderInfo->GetVersion(&version);

    PRInt64 fileSize;
    PRUint32 actualFolderTimeStamp;
    GetMailboxModProperties(&fileSize, &actualFolderTimeStamp);

    if (folderSize == fileSize &&
        numUnreadMessages >= 0 && GetCurVersion() == version)
    {
      GetGlobalPrefs();
      // if those values are ok, check time stamp
      if (gTimeStampLeeway == 0)
        *aResult = folderDate == actualFolderTimeStamp;
      else
        *aResult = PR_ABS((PRInt32) (actualFolderTimeStamp - folderDate)) <= gTimeStampLeeway;
#ifndef PUTUP_ALERT_ON_INVALID_DB
    }
  }
#else
      if (!*aResult)
      {
        errorMsg.AppendLiteral("time stamp didn't match delta = ");
        errorMsg.AppendInt(actualFolderTimeStamp - folderDate);
        errorMsg.AppendLiteral(" leeway = ");
        errorMsg.AppendInt(gTimeStampLeeway);
      }
    }
    else if (folderSize != fileSize)
    {
      errorMsg.AppendLiteral("folder size didn't match db size = ");
      errorMsg.AppendInt(folderSize);
      errorMsg.AppendLiteral(" actual size = ");
      errorMsg.AppendInt(fileSize);
    }
    else if (numUnreadMessages < 0)
    {
      errorMsg.AppendLiteral("numUnreadMessages < 0");
    }
  }
  if (errorMsg.Length())
  {
    nsCOMPtr<nsIPrompt> dialog;

    nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
    if (wwatch)
      wwatch->GetNewPrompter(0, getter_AddRefs(dialog));
    if (dialog)
      dialog->Alert(nsnull, errorMsg.get());
  }
#endif // PUTUP_ALERT_ON_INVALID_DB
  return NS_OK;
}

NS_IMETHODIMP nsMailDatabase::SetSummaryValid(PRBool valid)
{
  nsresult rv = NS_OK;
  PRBool exists;
  m_folderFile->Exists(&exists);
  if (!exists) 
    return NS_MSG_ERROR_FOLDER_MISSING;
  
  if (m_dbFolderInfo)
  {
    if (valid)
    {
      PRUint32 actualFolderTimeStamp;
      PRInt64 fileSize;
      GetMailboxModProperties(&fileSize, &actualFolderTimeStamp);
      m_dbFolderInfo->SetUint64Property("folderSize", fileSize);
      m_dbFolderInfo->SetFolderDate(actualFolderTimeStamp);
      m_dbFolderInfo->SetVersion(GetCurVersion());
    }
    else
    {
      m_dbFolderInfo->SetVersion(0);	// that ought to do the trick.
    }
  }
  Commit(nsMsgDBCommitType::kLargeCommit);
  return rv;
}

nsresult nsMailDatabase::GetFolderName(nsString &folderName)
{
  m_folderFile->GetPath(folderName);
  return NS_OK;
}


NS_IMETHODIMP  nsMailDatabase::RemoveOfflineOp(nsIMsgOfflineImapOperation *op)
{
  
  nsresult rv = GetAllOfflineOpsTable();
  NS_ENSURE_SUCCESS(rv, rv);
  
  if (!op || !m_mdbAllOfflineOpsTable)
    return NS_ERROR_NULL_POINTER;
  nsMsgOfflineImapOperation* offlineOp = static_cast<nsMsgOfflineImapOperation*>(op);  // closed system, so this is ok
  nsIMdbRow* row = offlineOp->GetMDBRow();
  rv = m_mdbAllOfflineOpsTable->CutRow(GetEnv(), row);
  row->CutAllColumns(GetEnv());
  return rv;
}

NS_IMETHODIMP nsMailDatabase::GetOfflineOpForKey(nsMsgKey msgKey, PRBool create, nsIMsgOfflineImapOperation **offlineOp)
{
  mdb_bool	hasOid;
  mdbOid		rowObjectId;
  mdb_err   err;
  
  if (!IMAPOffline)
    IMAPOffline = PR_NewLogModule("IMAPOFFLINE");
  nsresult rv = GetAllOfflineOpsTable();
  NS_ENSURE_SUCCESS(rv, rv);
  
  if (!offlineOp || !m_mdbAllOfflineOpsTable)
    return NS_ERROR_NULL_POINTER;
  
  *offlineOp = NULL;
  
  rowObjectId.mOid_Id = msgKey;
  rowObjectId.mOid_Scope = m_offlineOpsRowScopeToken;
  err = m_mdbAllOfflineOpsTable->HasOid(GetEnv(), &rowObjectId, &hasOid);
  if (err == NS_OK && m_mdbStore && (hasOid  || create))
  {
    nsCOMPtr <nsIMdbRow> offlineOpRow;
    err = m_mdbStore->GetRow(GetEnv(), &rowObjectId, getter_AddRefs(offlineOpRow));
    
    if (create)
    {
      if (!offlineOpRow)
      {
        err  = m_mdbStore->NewRowWithOid(GetEnv(), &rowObjectId, getter_AddRefs(offlineOpRow));
        NS_ENSURE_SUCCESS(err, err);
      }
      if (offlineOpRow && !hasOid)
        m_mdbAllOfflineOpsTable->AddRow(GetEnv(), offlineOpRow);
    }
    
    if (err == NS_OK && offlineOpRow)
    {
      *offlineOp = new nsMsgOfflineImapOperation(this, offlineOpRow);
      if (*offlineOp)
        (*offlineOp)->SetMessageKey(msgKey);
      NS_IF_ADDREF(*offlineOp);
    }
    if (!hasOid && m_dbFolderInfo)
    {
      // set initial value for flags so we don't lose them.
      nsCOMPtr <nsIMsgDBHdr> msgHdr;
      GetMsgHdrForKey(msgKey, getter_AddRefs(msgHdr));
      if (msgHdr)
      {
        PRUint32 flags;
        msgHdr->GetFlags(&flags);
        (*offlineOp)->SetNewFlags(flags);
      }
      PRInt32 newFlags;
      m_dbFolderInfo->OrFlags(nsMsgFolderFlags::OfflineEvents, &newFlags);
    }
  }
  
  return (err == 0) ? NS_OK : NS_ERROR_FAILURE;

}

NS_IMETHODIMP nsMailDatabase::EnumerateOfflineOps(nsISimpleEnumerator **enumerator)
{
  NS_ASSERTION(PR_FALSE, "not impl yet");
  return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP nsMailDatabase::ListAllOfflineOpIds(nsTArray<nsMsgKey> *offlineOpIds)
{
  NS_ENSURE_ARG(offlineOpIds);
  nsresult rv = GetAllOfflineOpsTable();
  NS_ENSURE_SUCCESS(rv, rv);
  nsIMdbTableRowCursor *rowCursor;
  if (!IMAPOffline)
    IMAPOffline = PR_NewLogModule("IMAPOFFLINE");

  if (m_mdbAllOfflineOpsTable)
  {
    nsresult err = m_mdbAllOfflineOpsTable->GetTableRowCursor(GetEnv(), -1, &rowCursor);
    while (err == NS_OK && rowCursor)
    {
      mdbOid outOid;
      mdb_pos	outPos;
      
      err = rowCursor->NextRowOid(GetEnv(), &outOid, &outPos);
      // is this right? Mork is returning a 0 id, but that should valid.
      if (outPos < 0 || outOid.mOid_Id == (mdb_id) -1)	
        break;
      if (err == NS_OK)
      {
        offlineOpIds->AppendElement(outOid.mOid_Id);
        if (PR_LOG_TEST(IMAPOffline, PR_LOG_ALWAYS))
        {
          nsCOMPtr <nsIMsgOfflineImapOperation> offlineOp;
          GetOfflineOpForKey(outOid.mOid_Id, PR_FALSE, getter_AddRefs(offlineOp));
          if (offlineOp)
          {
            nsMsgOfflineImapOperation *logOp = static_cast<nsMsgOfflineImapOperation *>(static_cast<nsIMsgOfflineImapOperation *>(offlineOp.get()));
            if (logOp)
              logOp->Log(IMAPOffline);

          }
        }
      }
    }
    rv = (err == NS_OK) ? NS_OK : NS_ERROR_FAILURE;
    rowCursor->Release();
  }
  
  offlineOpIds->Sort();
  return rv;
}

NS_IMETHODIMP nsMailDatabase::ListAllOfflineDeletes(nsTArray<nsMsgKey> *offlineDeletes)
{
  if (!offlineDeletes)
    return NS_ERROR_NULL_POINTER;
  
  nsresult rv = GetAllOfflineOpsTable();
  NS_ENSURE_SUCCESS(rv, rv);
  nsIMdbTableRowCursor *rowCursor;
  if (m_mdbAllOfflineOpsTable)
  {
    nsresult err = m_mdbAllOfflineOpsTable->GetTableRowCursor(GetEnv(), -1, &rowCursor);
    while (err == NS_OK && rowCursor)
    {
      mdbOid outOid;
      mdb_pos	outPos;
      nsIMdbRow* offlineOpRow;
      
      err = rowCursor->NextRow(GetEnv(), &offlineOpRow, &outPos);
      // is this right? Mork is returning a 0 id, but that should valid.
      if (outPos < 0 || offlineOpRow == nsnull)	
        break;
      if (err == NS_OK)
      {
        offlineOpRow->GetOid(GetEnv(), &outOid);
        nsIMsgOfflineImapOperation *offlineOp = new nsMsgOfflineImapOperation(this, offlineOpRow);
        if (offlineOp)
        {
          NS_ADDREF(offlineOp);
          imapMessageFlagsType newFlags;
          nsOfflineImapOperationType opType;
          
          offlineOp->GetOperation(&opType);
          offlineOp->GetNewFlags(&newFlags);
          if (opType & nsIMsgOfflineImapOperation::kMsgMoved || 
            ((opType & nsIMsgOfflineImapOperation::kFlagsChanged) 
            && (newFlags & nsIMsgOfflineImapOperation::kMsgMarkedDeleted)))
            offlineDeletes->AppendElement(outOid.mOid_Id);
          NS_RELEASE(offlineOp);
        }
        offlineOpRow->Release();
      }
    }
    rv = (err == NS_OK) ? NS_OK : NS_ERROR_FAILURE;
    rowCursor->Release();
  }
  return rv;
}

/* static */
nsresult nsMailDatabase::SetFolderInfoValid(nsILocalFile *folderName, int num, int numunread)
{
  nsresult err = NS_OK;
  PRBool bOpenedDB = PR_FALSE;
  nsCOMPtr <nsILocalFile> summaryPath;
  GetSummaryFileLocation(folderName, getter_AddRefs(summaryPath));
  
  PRBool exists;
  folderName->Exists(&exists);
  if (!exists)
    return NS_MSG_ERROR_FOLDER_SUMMARY_MISSING;
  
  // should we have type safe downcast methods again?
  nsMailDatabase *pMessageDB = (nsMailDatabase *) nsMailDatabase::FindInCache(summaryPath);
  if (pMessageDB == nsnull)
  {
    pMessageDB = new nsMailDatabase();
    if(!pMessageDB)
      return NS_ERROR_OUT_OF_MEMORY;
    
    pMessageDB->m_folderFile = folderName;
    
//    *(pMessageDB->m_folderSpec) = summaryPath;
    // ### this does later stuff (marks latered messages unread), which may be a problem
    nsCString summaryNativePath;
    summaryPath->GetNativePath(summaryNativePath);
    err = pMessageDB->OpenMDB(summaryNativePath.get(), PR_FALSE);
    if (err != NS_OK)
    {
      delete pMessageDB;
      pMessageDB = nsnull;
    }
    bOpenedDB = PR_TRUE;
  }
  
  if (pMessageDB == nsnull)
  {
#ifdef DEBUG
    printf("Exception opening summary file\n");
#endif
    return NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE;
  }
  
  {
    pMessageDB->m_folderFile = folderName;
    PRUint32 actualFolderTimeStamp;
    PRInt64 fileSize;
    pMessageDB->GetMailboxModProperties(&fileSize, &actualFolderTimeStamp);
    pMessageDB->m_dbFolderInfo->SetFolderSize64(fileSize);
    pMessageDB->m_dbFolderInfo->SetFolderDate(actualFolderTimeStamp);
    pMessageDB->m_dbFolderInfo->ChangeNumUnreadMessages(numunread);
    pMessageDB->m_dbFolderInfo->ChangeNumMessages(num);
  }
  // if we opened the db, then we'd better close it. Otherwise, we found it in the cache,
  // so just commit and release.
  if (bOpenedDB)
  {
    pMessageDB->Close(PR_TRUE);
  }
  else if (pMessageDB)
  { 
    err = pMessageDB->Commit(nsMsgDBCommitType::kLargeCommit);
    pMessageDB->Release();
  }
  return err;
}


// This is used to remember that the db is out of sync with the mail folder
// and needs to be regenerated.
void nsMailDatabase::SetReparse(PRBool reparse)
{
  m_reparse = reparse;
}


class nsMsgOfflineOpEnumerator : public nsISimpleEnumerator {
public:
  NS_DECL_ISUPPORTS

  // nsISimpleEnumerator methods:
  NS_DECL_NSISIMPLEENUMERATOR

  nsMsgOfflineOpEnumerator(nsMailDatabase* db);
  virtual ~nsMsgOfflineOpEnumerator();

protected:
  nsresult					GetRowCursor();
  nsresult					PrefetchNext();
  nsMailDatabase*              mDB;
  nsIMdbTableRowCursor*       mRowCursor;
  nsCOMPtr <nsIMsgOfflineImapOperation> mResultOp;
  PRBool            mDone;
  PRBool						mNextPrefetched;
};

nsMsgOfflineOpEnumerator::nsMsgOfflineOpEnumerator(nsMailDatabase* db)
    : mDB(db), mRowCursor(nsnull), mDone(PR_FALSE)
{
  NS_ADDREF(mDB);
  mNextPrefetched = PR_FALSE;
}

nsMsgOfflineOpEnumerator::~nsMsgOfflineOpEnumerator()
{
  NS_IF_RELEASE(mRowCursor);
  NS_RELEASE(mDB);
}

NS_IMPL_ISUPPORTS1(nsMsgOfflineOpEnumerator, nsISimpleEnumerator)

nsresult nsMsgOfflineOpEnumerator::GetRowCursor()
{
  nsresult rv = 0;
  mDone = PR_FALSE;

  if (!mDB || !mDB->m_mdbAllOfflineOpsTable)
    return NS_ERROR_NULL_POINTER;

  rv = mDB->m_mdbAllOfflineOpsTable->GetTableRowCursor(mDB->GetEnv(), -1, &mRowCursor);
  return rv;
}

NS_IMETHODIMP nsMsgOfflineOpEnumerator::GetNext(nsISupports **aItem)
{
  if (!aItem)
    return NS_ERROR_NULL_POINTER;
  nsresult rv=NS_OK;
  if (!mNextPrefetched)
    rv = PrefetchNext();
  if (NS_SUCCEEDED(rv))
  {
    if (mResultOp) 
    {
      *aItem = mResultOp;
      NS_ADDREF(*aItem);
      mNextPrefetched = PR_FALSE;
    }
  }
  return rv;
}

nsresult nsMsgOfflineOpEnumerator::PrefetchNext()
{
  nsresult rv = NS_OK;
  nsIMdbRow* offlineOpRow;
  mdb_pos rowPos;

  if (!mRowCursor)
  {
    rv = GetRowCursor();
    if (NS_FAILED(rv))
      return rv;
  }

  rv = mRowCursor->NextRow(mDB->GetEnv(), &offlineOpRow, &rowPos);
  if (!offlineOpRow) 
  {
    mDone = PR_TRUE;
    return NS_ERROR_FAILURE;
  }
  if (NS_FAILED(rv)) 
  {
    mDone = PR_TRUE;
    return rv;
  }
	//Get key from row
  mdbOid outOid;
  nsMsgKey key=0;
  if (offlineOpRow->GetOid(mDB->GetEnv(), &outOid) == NS_OK)
    key = outOid.mOid_Id;

  nsIMsgOfflineImapOperation *op = new nsMsgOfflineImapOperation(mDB, offlineOpRow);
  mResultOp = op;
  if (!op)
    return NS_ERROR_OUT_OF_MEMORY;

  if (mResultOp) 
  {
    mNextPrefetched = PR_TRUE;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgOfflineOpEnumerator::HasMoreElements(PRBool *aResult)
{
  if (!aResult)
    return NS_ERROR_NULL_POINTER;

  if (!mNextPrefetched)
    PrefetchNext();
  *aResult = !mDone;
  return NS_OK;
}
