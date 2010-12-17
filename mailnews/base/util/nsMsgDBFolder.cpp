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
 *   Pierre Phaneuf <pp@ludusdesign.com>
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
#include "nsUnicharUtils.h"
#include "nsMsgDBFolder.h"
#include "nsMsgFolderFlags.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "nsRDFCID.h"
#include "nsNetUtil.h"
#include "nsIMsgFolderCache.h"
#include "nsIMsgFolderCacheElement.h"
#include "nsMsgBaseCID.h"
#include "nsIMsgMailNewsUrl.h"
#include "nsMsgDatabase.h"
#include "nsIMsgAccountManager.h"
#include "nsISeekableStream.h"
#include "nsNativeCharsetUtils.h"
#include "nsIChannel.h"
#include "nsITransport.h"
#include "nsIMsgFolderCompactor.h"
#include "nsIDocShell.h"
#include "nsIMsgWindow.h"
#include "nsIPrompt.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILocale.h"
#include "nsILocaleService.h"
#include "nsCollationCID.h"
#include "nsAbBaseCID.h"
#include "nsIAbCard.h"
#include "nsIAbDirectory.h"
#include "nsISpamSettings.h"
#include "nsIMsgFilterPlugin.h"
#include "nsIMsgMailSession.h"
#include "nsIRDFService.h"
#include "nsTextFormatter.h"
#include "nsMsgDBCID.h"
#include "nsInt64.h"
#include "nsReadLine.h"
#include "nsParserCIID.h"
#include "nsIParser.h"
#include "nsIHTMLContentSink.h"
#include "nsIContentSerializer.h"
#include "nsLayoutCID.h"
#include "nsIHTMLToTextSink.h"
#include "nsIDocumentEncoder.h"
#include "nsMsgI18N.h"
#include "nsIMIMEHeaderParam.h"
#include "plbase64.h"
#include "nsArrayEnumerator.h"
#include <time.h>
#include "nsIMsgFolderNotificationService.h"
#include "nsIMutableArray.h"
#include "nsArrayUtils.h"
#include "nsIMimeHeaders.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIMsgTraitService.h"
#include "nsIMessenger.h"
#include "nsITransactionManager.h"
#include "nsMsgReadStateTxn.h"
#include "nsAutoPtr.h"
#include "nsIPK11TokenDB.h"
#include "nsIPK11Token.h"
#include "nsMsgLocalFolderHdrs.h"
#define oneHour 3600000000U
#include "nsMsgUtils.h"
#include "nsIMsgFilterService.h"
#include "nsDirectoryServiceUtils.h"

static PRTime gtimeOfLastPurgeCheck;    //variable to know when to check for purge_threshhold

#define PREF_MAIL_PROMPT_PURGE_THRESHOLD "mail.prompt_purge_threshhold"
#define PREF_MAIL_PURGE_THRESHOLD "mail.purge_threshhold"
#define PREF_MAIL_PURGE_ASK "mail.purge.ask"
#define PREF_MAIL_WARN_FILTER_CHANGED "mail.warn_filter_changed"

static NS_DEFINE_CID(kRDFServiceCID, NS_RDFSERVICE_CID);
static NS_DEFINE_CID(kCMailDB, NS_MAILDB_CID);
static NS_DEFINE_CID(kParserCID, NS_PARSER_CID);

nsICollation * nsMsgDBFolder::gCollationKeyGenerator = nsnull;

PRUnichar *nsMsgDBFolder::kLocalizedInboxName;
PRUnichar *nsMsgDBFolder::kLocalizedTrashName;
PRUnichar *nsMsgDBFolder::kLocalizedSentName;
PRUnichar *nsMsgDBFolder::kLocalizedDraftsName;
PRUnichar *nsMsgDBFolder::kLocalizedTemplatesName;
PRUnichar *nsMsgDBFolder::kLocalizedUnsentName;
PRUnichar *nsMsgDBFolder::kLocalizedJunkName;
PRUnichar *nsMsgDBFolder::kLocalizedArchivesName;

PRUnichar *nsMsgDBFolder::kLocalizedBrandShortName;

nsrefcnt nsMsgDBFolder::mInstanceCount=0;

NS_IMPL_ISUPPORTS_INHERITED6(nsMsgDBFolder, nsRDFResource, 
                             nsISupportsWeakReference, nsIMsgFolder,
                             nsIDBChangeListener, nsIUrlListener,
                             nsIJunkMailClassificationListener,
                             nsIMsgTraitClassificationListener)

#define MSGDBFOLDER_ATOM(name_, value_) nsIAtom* nsMsgDBFolder::name_ = nsnull;
#include "nsMsgDBFolderAtomList.h"
#undef MSGDBFOLDER_ATOM

#ifdef MOZILLA_1_9_2_BRANCH

const nsStaticAtom nsMsgDBFolder::folder_atoms[] = {
#define MSGDBFOLDER_ATOM(name_, value_) { value_, &nsMsgDBFolder::name_ },
#include "nsMsgDBFolderAtomList.h"
#undef MSGDBFOLDER_ATOM
};

#else // i.e. !MOZILLA_1_9_2_BRANCH

#define MSGDBFOLDER_ATOM(name_, value_) NS_STATIC_ATOM_BUFFER(name_##_buffer, value_)
#include "nsMsgDBFolderAtomList.h"
#undef MSGDBFOLDER_ATOM

const nsStaticAtom nsMsgDBFolder::folder_atoms[] = {
#define MSGDBFOLDER_ATOM(name_, value_) NS_STATIC_ATOM(name_##_buffer, &nsMsgDBFolder::name_),
#include "nsMsgDBFolderAtomList.h"
#undef MSGDBFOLDER_ATOM
};

#endif // end MOZILLA_1_9_2_BRANCH

nsMsgDBFolder::nsMsgDBFolder(void)
: mAddListener(PR_TRUE),
  mNewMessages(PR_FALSE),
  mGettingNewMessages(PR_FALSE),
  mLastMessageLoaded(nsMsgKey_None),
  mFlags(0),
  mNumUnreadMessages(-1),
  mNumTotalMessages(-1),
  mNotifyCountChanges(PR_TRUE),
  mExpungedBytes(0),
  mInitializedFromCache(PR_FALSE),
  mSemaphoreHolder(nsnull),
  mNumPendingUnreadMessages(0),
  mNumPendingTotalMessages(0),
  mFolderSize(0),
  mNumNewBiffMessages(0),
  mIsCachable(PR_TRUE),
  mHaveParsedURI(PR_FALSE),
  mIsServerIsValid(PR_FALSE),
  mIsServer(PR_FALSE),
  mInVFEditSearchScope (PR_FALSE)
{
  if (mInstanceCount++ <=0) {
#ifdef MOZILLA_INTERNAL_API //FIXME NS_RegisterStaticAtoms
    NS_RegisterStaticAtoms(folder_atoms, NS_ARRAY_LENGTH(folder_atoms));
#else
    NS_ERROR("NS_RegisterStaticAtoms not implemented in frozen linkage");
#endif
    initializeStrings();
    createCollationKeyGenerator();
#ifdef MSG_FASTER_URI_PARSING
    mParsingURL = do_CreateInstance(NS_STANDARDURL_CONTRACTID);
#endif
    LL_I2L(gtimeOfLastPurgeCheck, 0);
  }

  mProcessingFlag[0].bit = nsMsgProcessingFlags::ClassifyJunk;
  mProcessingFlag[1].bit = nsMsgProcessingFlags::ClassifyTraits;
  mProcessingFlag[2].bit = nsMsgProcessingFlags::TraitsDone;
  mProcessingFlag[3].bit = nsMsgProcessingFlags::FiltersDone;
  mProcessingFlag[4].bit = nsMsgProcessingFlags::FilterToMove;
  mProcessingFlag[5].bit = nsMsgProcessingFlags::NotReportedClassified;
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
    mProcessingFlag[i].keys = nsMsgKeySetU::Create();
}

nsMsgDBFolder::~nsMsgDBFolder(void)
{
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
    delete mProcessingFlag[i].keys;

  if (--mInstanceCount == 0) {
    NS_IF_RELEASE(gCollationKeyGenerator);
    NS_Free(kLocalizedInboxName);
    NS_Free(kLocalizedTrashName);
    NS_Free(kLocalizedSentName);
    NS_Free(kLocalizedDraftsName);
    NS_Free(kLocalizedTemplatesName);
    NS_Free(kLocalizedUnsentName);
    NS_Free(kLocalizedJunkName);
    NS_Free(kLocalizedArchivesName);
    NS_Free(kLocalizedBrandShortName);
#ifdef MSG_FASTER_URI_PARSING
    mParsingURL = nsnull;
#endif
  }
  //shutdown but don't shutdown children.
  Shutdown(PR_FALSE);
}

NS_IMETHODIMP nsMsgDBFolder::Shutdown(PRBool shutdownChildren)
{
  if(mDatabase)
  {
    mDatabase->RemoveListener(this);
    mDatabase->ForceClosed();
    mDatabase = nsnull;
    if (mBackupDatabase)
    {
      mBackupDatabase->ForceClosed();
      mBackupDatabase = nsnull;
    }
  }

  if(shutdownChildren)
  {
    PRInt32 count = mSubFolders.Count();

    for (PRInt32 i = 0; i < count; i++)
      mSubFolders[i]->Shutdown(PR_TRUE);

    // Reset incoming server pointer and pathname.
    mServer = nsnull;
    mPath = nsnull;
    mHaveParsedURI = PR_FALSE;
    mName.Truncate();
    mSubFolders.Clear();
  }
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::ForceDBClosed()
{
  PRInt32 count = mSubFolders.Count();
  for (PRInt32 i = 0; i < count; i++)
    mSubFolders[i]->ForceDBClosed();

  if (mDatabase)
  {
    mDatabase->ForceClosed();
    mDatabase = nsnull;
  }
  else
  {
    nsCOMPtr<nsIMsgDatabase> mailDBFactory = do_CreateInstance(kCMailDB);
    if (mailDBFactory)
      mailDBFactory->ForceFolderDBClosed(this);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::CloseAndBackupFolderDB(const nsACString& newName)
{
  ForceDBClosed();

  // We only support backup for mail at the moment
  if ( !(mFlags & nsMsgFolderFlags::Mail))
    return NS_OK;

  nsCOMPtr<nsILocalFile> folderPath;
  nsresult rv = GetFilePath(getter_AddRefs(folderPath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> dbFile;
  rv = GetSummaryFileLocation(folderPath, getter_AddRefs(dbFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDir;
  rv = CreateBackupDirectory(getter_AddRefs(backupDir));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDBFile;
  rv = GetBackupSummaryFile(getter_AddRefs(backupDBFile), newName);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mBackupDatabase)
  {
    mBackupDatabase->ForceClosed();
    mBackupDatabase = nsnull;
  }

  backupDBFile->Remove(PR_FALSE);
  PRBool backupExists;
  backupDBFile->Exists(&backupExists);
  NS_ASSERTION(!backupExists, "Couldn't delete database backup");
  if (backupExists)
    return NS_ERROR_FAILURE;

  if (!newName.IsEmpty())
  {
    nsCAutoString backupName;
    rv = backupDBFile->GetNativeLeafName(backupName);
    NS_ENSURE_SUCCESS(rv, rv);
    return dbFile->CopyToNative(backupDir, backupName);
  }
  else
    return dbFile->CopyToNative(backupDir, EmptyCString());
}

NS_IMETHODIMP nsMsgDBFolder::OpenBackupMsgDatabase()
{
  if (mBackupDatabase)
    return NS_OK;
  nsCOMPtr<nsILocalFile> folderPath;
  nsresult rv = GetFilePath(getter_AddRefs(folderPath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString folderName;
  rv = folderPath->GetLeafName(folderName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDir;
  rv = CreateBackupDirectory(getter_AddRefs(backupDir));
  NS_ENSURE_SUCCESS(rv, rv);

  // We use a dummy message folder file so we can use
  // GetSummaryFileLocation to get the db file name
  nsCOMPtr<nsILocalFile> backupDBDummyFolder;
  rv = CreateBackupDirectory(getter_AddRefs(backupDBDummyFolder));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = backupDBDummyFolder->Append(folderName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIMsgDBService> msgDBService =
      do_GetService(NS_MSGDB_SERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = msgDBService->OpenMailDBFromFile(
      backupDBDummyFolder, PR_FALSE, PR_TRUE, getter_AddRefs(mBackupDatabase));
  // we add a listener so that we can close the db during OnAnnouncerGoingAway. There should
  // not be any other calls to the listener with the backup database
  if (NS_SUCCEEDED(rv) && mBackupDatabase)
    mBackupDatabase->AddListener(this);

  if (rv == NS_MSG_ERROR_FOLDER_SUMMARY_OUT_OF_DATE)
    // this is normal in reparsing
    rv = NS_OK;
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::RemoveBackupMsgDatabase()
{
  nsCOMPtr<nsILocalFile> folderPath;
  nsresult rv = GetFilePath(getter_AddRefs(folderPath));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString folderName;
  rv = folderPath->GetLeafName(folderName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDir;
  rv = CreateBackupDirectory(getter_AddRefs(backupDir));
  NS_ENSURE_SUCCESS(rv, rv);

  // We use a dummy message folder file so we can use
  // GetSummaryFileLocation to get the db file name
  nsCOMPtr<nsILocalFile> backupDBDummyFolder;
  rv = CreateBackupDirectory(getter_AddRefs(backupDBDummyFolder));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = backupDBDummyFolder->Append(folderName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDBFile;
  rv = GetSummaryFileLocation(backupDBDummyFolder, getter_AddRefs(backupDBFile));
  NS_ENSURE_SUCCESS(rv, rv);

  if (mBackupDatabase)
  {
    mBackupDatabase->ForceClosed();
    mBackupDatabase = nsnull;
  }

  return backupDBFile->Remove(PR_FALSE);
}  

NS_IMETHODIMP nsMsgDBFolder::StartFolderLoading(void)
{
  if(mDatabase)
    mDatabase->RemoveListener(this);
  mAddListener = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::EndFolderLoading(void)
{
  if(mDatabase)
    mDatabase->AddListener(this);
  mAddListener = PR_TRUE;
  UpdateSummaryTotals(PR_TRUE);

  //GGGG       check for new mail here and call SetNewMessages...?? -- ONE OF THE 2 PLACES
  if(mDatabase)
    m_newMsgs.Clear();

  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetExpungedBytes(PRUint32 *count)
{
  NS_ENSURE_ARG_POINTER(count);

  if (mDatabase)
  {
    nsresult rv;
    nsCOMPtr<nsIDBFolderInfo> folderInfo;
    rv = mDatabase->GetDBFolderInfo(getter_AddRefs(folderInfo));
    if (NS_FAILED(rv)) return rv;
    rv = folderInfo->GetExpungedBytes((PRInt32 *) count);
    if (NS_SUCCEEDED(rv))
      mExpungedBytes = *count; // sync up with the database
    return rv;
  }
  else
  {
    ReadDBFolderInfo(PR_FALSE);
    *count = mExpungedBytes;
  }
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::GetCharset(nsACString& aCharset)
{
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  nsCOMPtr<nsIMsgDatabase> db;
  nsresult rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if(NS_SUCCEEDED(rv))
    rv = folderInfo->GetEffectiveCharacterSet(aCharset);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SetCharset(const nsACString& aCharset)
{
  nsresult rv;

  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  nsCOMPtr<nsIMsgDatabase> db;
  rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if(NS_SUCCEEDED(rv))
  {
    rv = folderInfo->SetCharacterSet(aCharset);
    db->Commit(nsMsgDBCommitType::kLargeCommit);
    mCharset = aCharset;
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetCharsetOverride(PRBool *aCharsetOverride)
{
  NS_ENSURE_ARG_POINTER(aCharsetOverride);
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  nsCOMPtr<nsIMsgDatabase> db;
  nsresult rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if(NS_SUCCEEDED(rv))
    rv = folderInfo->GetCharacterSetOverride(aCharsetOverride);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SetCharsetOverride(PRBool aCharsetOverride)
{
  nsresult rv;
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  nsCOMPtr<nsIMsgDatabase> db;
  rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if(NS_SUCCEEDED(rv))
  {
    rv = folderInfo->SetCharacterSetOverride(aCharsetOverride);
    db->Commit(nsMsgDBCommitType::kLargeCommit);
    mCharsetOverride = aCharsetOverride;  // synchronize member variable
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetHasNewMessages(PRBool *hasNewMessages)
{
  NS_ENSURE_ARG_POINTER(hasNewMessages);
  *hasNewMessages = mNewMessages;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetHasNewMessages(PRBool curNewMessages)
{
  if (curNewMessages != mNewMessages)
  {
    // Only change mru time if we're going from doesn't have new to has new.
    // technically, we should probably update mru time for every new message
    // but we would pay a performance penalty for that. If the user
    // opens the folder, the mrutime will get updated anyway.
    if (curNewMessages)
      SetMRUTime();
    PRBool oldNewMessages = mNewMessages;
    mNewMessages = curNewMessages;
    NotifyBoolPropertyChanged(kNewMessagesAtom, oldNewMessages, curNewMessages);
  }

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetGettingNewMessages(PRBool *gettingNewMessages)
{
  NS_ENSURE_ARG_POINTER(gettingNewMessages);
  *gettingNewMessages = mGettingNewMessages;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetGettingNewMessages(PRBool gettingNewMessages)
{
  mGettingNewMessages = gettingNewMessages;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetFirstNewMessage(nsIMsgDBHdr **firstNewMessage)
{
  //If there's not a db then there can't be new messages.  Return failure since you
  //should use HasNewMessages first.
  if(!mDatabase)
    return NS_ERROR_FAILURE;

  nsresult rv;
  nsMsgKey key;
  rv = mDatabase->GetFirstNew(&key);
  if(NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIMsgDBHdr> hdr;
  rv = mDatabase->GetMsgHdrForKey(key, getter_AddRefs(hdr));
  if(NS_FAILED(rv))
    return rv;

  return  mDatabase->GetMsgHdrForKey(key, firstNewMessage);
}

NS_IMETHODIMP nsMsgDBFolder::ClearNewMessages()
{
  nsresult rv = NS_OK;
  //If there's no db then there's nothing to clear.
  if(mDatabase)
  {
    PRUint32 numNewKeys;
    PRUint32 *newMessageKeys;
    rv = mDatabase->GetNewList(&numNewKeys, &newMessageKeys);
    if (NS_SUCCEEDED(rv) && newMessageKeys)
    {
      m_saveNewMsgs.Clear();
      m_saveNewMsgs.AppendElements(newMessageKeys, numNewKeys);
      NS_Free(newMessageKeys);
    }
    mDatabase->ClearNewList(PR_TRUE);
  }
  m_newMsgs.Clear();
  mNumNewBiffMessages = 0;
  return rv;
}

void nsMsgDBFolder::UpdateNewMessages()
{
  if (! (mFlags & nsMsgFolderFlags::Virtual))
  {
    PRBool hasNewMessages = PR_FALSE;
    for (PRUint32 keyIndex = 0; keyIndex < m_newMsgs.Length(); keyIndex++)
    {
      PRBool containsKey = PR_FALSE;
      mDatabase->ContainsKey(m_newMsgs[keyIndex], &containsKey);
      if (!containsKey)
        continue;
      PRBool isRead = PR_FALSE;
      nsresult rv2 = mDatabase->IsRead(m_newMsgs[keyIndex], &isRead);
      if (NS_SUCCEEDED(rv2) && !isRead)
      {
        hasNewMessages = PR_TRUE;
        mDatabase->AddToNewList(m_newMsgs[keyIndex]);
      }
    }
    SetHasNewMessages(hasNewMessages);
  }
}

// helper function that gets the cache element that corresponds to the passed in file spec.
// This could be static, or could live in another class - it's not specific to the current
// nsMsgDBFolder. If it lived at a higher level, we could cache the account manager and folder cache.
nsresult nsMsgDBFolder::GetFolderCacheElemFromFile(nsILocalFile *file, nsIMsgFolderCacheElement **cacheElement)
{
  nsresult result;
  NS_ENSURE_ARG_POINTER(file);
  NS_ENSURE_ARG_POINTER(cacheElement);
  nsCOMPtr <nsIMsgFolderCache> folderCache;
#ifdef DEBUG_bienvenu1
  PRBool exists;
  NS_ASSERTION(NS_SUCCEEDED(fileSpec->Exists(&exists)) && exists, "whoops, file doesn't exist, mac will break");
#endif
  nsCOMPtr<nsIMsgAccountManager> accountMgr =
    do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &result);
  if(NS_SUCCEEDED(result))
  {
    result = accountMgr->GetFolderCache(getter_AddRefs(folderCache));
    if (NS_SUCCEEDED(result) && folderCache)
    {
      nsCString persistentPath;
      file->GetPersistentDescriptor(persistentPath);
      result = folderCache->GetCacheElement(persistentPath, PR_FALSE, cacheElement);
    }
  }
  return result;
}

nsresult nsMsgDBFolder::ReadDBFolderInfo(PRBool force)
{
  // Since it turns out to be pretty expensive to open and close
  // the DBs all the time, if we have to open it once, get everything
  // we might need while we're here
  nsresult result = NS_OK;

  // don't need to reload from cache if we've already read from cache,
  // and, we might get stale info, so don't do it.
  if (!mInitializedFromCache)
  {
    nsCOMPtr <nsILocalFile> dbPath;
    result = GetFolderCacheKey(getter_AddRefs(dbPath), PR_TRUE /* createDBIfMissing */);
    if (dbPath)
    {
      nsCOMPtr <nsIMsgFolderCacheElement> cacheElement;
      result = GetFolderCacheElemFromFile(dbPath, getter_AddRefs(cacheElement));
      if (NS_SUCCEEDED(result) && cacheElement)
        result = ReadFromFolderCacheElem(cacheElement);
    }
  }

  if (force || !mInitializedFromCache)
  {
    nsCOMPtr<nsIDBFolderInfo> folderInfo;
    nsCOMPtr<nsIMsgDatabase> db;
    result = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
    if(NS_SUCCEEDED(result))
    {
      mIsCachable = PR_TRUE;
      if (folderInfo)
      {
        if (!mInitializedFromCache)
        {
          folderInfo->GetFlags((PRInt32 *)&mFlags);
#ifdef DEBUG_bienvenu1
          nsString name;
          GetName(name);
          NS_ASSERTION(Compare(name, kLocalizedTrashName) || (mFlags & nsMsgFolderFlags::Trash), "lost trash flag");
#endif
          mInitializedFromCache = PR_TRUE;
        }

        folderInfo->GetNumMessages(&mNumTotalMessages);
        folderInfo->GetNumUnreadMessages(&mNumUnreadMessages);
        folderInfo->GetExpungedBytes((PRInt32 *)&mExpungedBytes);

        nsCString utf8Name;
        folderInfo->GetFolderName(utf8Name);
        if (!utf8Name.IsEmpty())
          CopyUTF8toUTF16(utf8Name, mName);

        //These should be put in IMAP folder only.
        //folderInfo->GetImapTotalPendingMessages(&mNumPendingTotalMessages);
        //folderInfo->GetImapUnreadPendingMessages(&mNumPendingUnreadMessages);

        folderInfo->GetCharacterSet(mCharset);
        folderInfo->GetCharacterSetOverride(&mCharsetOverride);

        if (db) {
          PRBool hasnew;
          nsresult rv;
          rv = db->HasNew(&hasnew);
          if (NS_FAILED(rv)) return rv;
          if (!hasnew && mNumPendingUnreadMessages <= 0)
            ClearFlag(nsMsgFolderFlags::GotNew);
        }
      }
    }
    else {
      // we tried to open DB but failed - don't keep trying.
      // If a DB is created, we will call this method with force == TRUE,
      // and read from the db that way.
      mInitializedFromCache = PR_TRUE;
    }
    if (db)
      db->Close(PR_FALSE);
  }
  return result;
}

nsresult nsMsgDBFolder::SendFlagNotifications(nsIMsgDBHdr *item, PRUint32 oldFlags, PRUint32 newFlags)
{
  nsresult rv = NS_OK;
  PRUint32 changedFlags = oldFlags ^ newFlags;
  if((changedFlags & nsMsgMessageFlags::Read)  && (changedFlags & nsMsgMessageFlags::New))
  {
    //..so..if the msg is read in the folder and the folder has new msgs clear the account level and status bar biffs.
    rv = NotifyPropertyFlagChanged(item, kStatusAtom, oldFlags, newFlags);
    rv = SetBiffState(nsMsgBiffState_NoMail);
  }
  else if(changedFlags & (nsMsgMessageFlags::Read | nsMsgMessageFlags::Replied | nsMsgMessageFlags::Forwarded
    | nsMsgMessageFlags::IMAPDeleted | nsMsgMessageFlags::New | nsMsgMessageFlags::Offline))
    rv = NotifyPropertyFlagChanged(item, kStatusAtom, oldFlags, newFlags);
  else if((changedFlags & nsMsgMessageFlags::Marked))
    rv = NotifyPropertyFlagChanged(item, kFlaggedAtom, oldFlags, newFlags);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::DownloadMessagesForOffline(nsIArray *messages, nsIMsgWindow *)
{
  NS_ASSERTION(PR_FALSE, "imap and news need to override this");
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::DownloadAllForOffline(nsIUrlListener *listener, nsIMsgWindow *msgWindow)
{
  NS_ASSERTION(PR_FALSE, "imap and news need to override this");
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetOfflineStoreInputStream(nsIInputStream **stream)
{
  nsCOMPtr <nsILocalFile> localStore;
  nsresult rv = GetFilePath(getter_AddRefs(localStore));
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_NewLocalFileInputStream(stream, localStore);
}

PRBool nsMsgDBFolder::VerifyOfflineMessage(nsIMsgDBHdr *msgHdr, nsIInputStream *fileStream)
{
  nsCOMPtr <nsISeekableStream> seekableStream = do_QueryInterface(fileStream);
  if (seekableStream)
  {
    PRUint64 offset;
    msgHdr->GetMessageOffset(&offset);
    nsresult rv = seekableStream->Seek(nsISeekableStream::NS_SEEK_CUR, offset);
    char startOfMsg[100];
    PRUint32 bytesRead = 0;
    PRUint32 bytesToRead = sizeof(startOfMsg) - 1;
    if (NS_SUCCEEDED(rv))
      rv = fileStream->Read(startOfMsg, bytesToRead, &bytesRead);
    startOfMsg[bytesRead] = '\0';
    // check if message starts with From, or is a draft and starts with FCC
    if (NS_FAILED(rv) || bytesRead != bytesToRead ||
      (strncmp(startOfMsg, "From ", 5) && (! (mFlags & nsMsgFolderFlags::Drafts) || strncmp(startOfMsg, "FCC", 3))))
      return PR_FALSE;
  }
  return PR_TRUE;
}

NS_IMETHODIMP nsMsgDBFolder::GetOfflineFileStream(nsMsgKey msgKey, PRUint64 *offset, PRUint32 *size, nsIInputStream **aFileStream)
{
  NS_ENSURE_ARG(aFileStream);

  *offset = *size = 0;

  nsCOMPtr <nsILocalFile> localStore;
  nsresult rv = GetFilePath(getter_AddRefs(localStore));
  NS_ENSURE_SUCCESS(rv, rv);
  if (localStore)
  {
    rv = NS_NewLocalFileInputStream(aFileStream, localStore);
    if (NS_SUCCEEDED(rv))
    {
      rv = GetDatabase();
      NS_ENSURE_SUCCESS(rv, NS_OK);
      nsCOMPtr<nsIMsgDBHdr> hdr;
      rv = mDatabase->GetMsgHdrForKey(msgKey, getter_AddRefs(hdr));
      if (hdr && NS_SUCCEEDED(rv))
      {
        hdr->GetMessageOffset(offset);
        hdr->GetOfflineMessageSize(size);
      }
      // check if offline store really has the correct offset into the offline
      // store by reading the first few bytes. If it doesn't, clear the offline
      // flag on the msg and return false, which will fall back to reading the message
      // from the server.
      // We'll also advance the offset past the envelope header and
      // X-Mozilla-Status lines.
      nsCOMPtr <nsISeekableStream> seekableStream = do_QueryInterface(*aFileStream);
      if (seekableStream)
      {
        rv = seekableStream->Seek(nsISeekableStream::NS_SEEK_CUR, *offset);
        char startOfMsg[200];
        PRUint32 bytesRead = 0;
        PRUint32 bytesToRead = sizeof(startOfMsg) - 1;
        if (NS_SUCCEEDED(rv))
          rv = (*aFileStream)->Read(startOfMsg, bytesToRead, &bytesRead);
        startOfMsg[bytesRead] = '\0';
        // check if message starts with From, or is a draft and starts with FCC
        if (NS_FAILED(rv) || bytesRead != bytesToRead ||
          (strncmp(startOfMsg, "From ", 5) && (! (mFlags & nsMsgFolderFlags::Drafts) || strncmp(startOfMsg, "FCC", 3))))
          rv = NS_ERROR_FAILURE;
        else
        {
          PRUint32 msgOffset = 0;
          // skip "From "/FCC line
          PRBool foundNextLine = MsgAdvanceToNextLine(startOfMsg, msgOffset, bytesRead - 1);
          if (foundNextLine && !strncmp(startOfMsg + msgOffset,
                                        X_MOZILLA_STATUS, X_MOZILLA_STATUS_LEN))
          {
            // skip X-Mozilla-Status line
            if (MsgAdvanceToNextLine(startOfMsg, msgOffset, bytesRead - 1))
            {
              if (!strncmp(startOfMsg + msgOffset,
                           X_MOZILLA_STATUS2, X_MOZILLA_STATUS2_LEN))
                 MsgAdvanceToNextLine(startOfMsg, msgOffset, bytesRead - 1);
            }
          }
          PRInt32 findPos = MsgFindCharInSet(nsDependentCString(startOfMsg),
                                             ":\n\r", msgOffset);
          // Check that the first line is a header line, i.e., with a ':' in it
          if (findPos != -1 && startOfMsg[findPos] == ':')
          {
            *offset += msgOffset;
            *size -= msgOffset;
          }
          else
          {
            rv = NS_ERROR_FAILURE;
          }
        }
      }
    }
    if (NS_FAILED(rv) && mDatabase)
      mDatabase->MarkOffline(msgKey, PR_FALSE, nsnull);
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetOfflineStoreOutputStream(nsIOutputStream **outputStream)
{
  NS_ENSURE_ARG_POINTER(outputStream);
  nsresult rv = NS_ERROR_NULL_POINTER;
    // the following code doesn't work for a host of reasons - the transfer offset
    // is ignored for output streams. The buffering used by file channels does not work
    // if transfer offsets are coerced to work, etc.
#if 0
    nsCOMPtr<nsIFileChannel> fileChannel = do_CreateInstance(NS_LOCALFILECHANNEL_CONTRACTID);
    if (fileChannel)
    {
      nsCOMPtr <nsILocalFile> localStore;
      rv = NS_NewLocalFile(nativePath, PR_TRUE, getter_AddRefs(localStore));
      if (NS_SUCCEEDED(rv) && localStore)
      {
        rv = fileChannel->Init(localStore, PR_CREATE_FILE | PR_RDWR, 0);
        if (NS_FAILED(rv))
          return rv;
        rv = fileChannel->Open(outputStream);
        if (NS_FAILED(rv))
          return rv;
      }
    }
#endif
  nsCOMPtr <nsILocalFile> localPath;
  rv = GetFilePath(getter_AddRefs(localPath));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = MsgNewBufferedFileOutputStream(outputStream, localPath, PR_WRONLY | PR_CREATE_FILE, 00600);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr <nsISeekableStream> seekable = do_QueryInterface(*outputStream);
  if (seekable)
    seekable->Seek(nsISeekableStream::NS_SEEK_END, 0);
  return rv;
}

// path coming in is the root path without the leaf name,
// on the way out, it's the whole path.
nsresult nsMsgDBFolder::CreateFileForDB(const nsAString& userLeafName, nsILocalFile *path, nsILocalFile **dbFile)
{
  NS_ENSURE_ARG_POINTER(dbFile);

  nsAutoString proposedDBName(userLeafName);
  NS_MsgHashIfNecessary(proposedDBName);

  // (note, the caller of this will be using the dbFile to call db->Open()
  // will turn the path into summary file path, and append the ".msf" extension)
  //
  // we want db->Open() to create a new summary file
  // so we have to jump through some hoops to make sure the .msf it will
  // create is unique.  now that we've got the "safe" proposedDBName,
  // we append ".msf" to see if the file exists.  if so, we make the name
  // unique and then string off the ".msf" so that we pass the right thing
  // into Open().  this isn't ideal, since this is not atomic
  // but it will make do.
  nsresult rv;
  nsCOMPtr <nsILocalFile> dbPath = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  dbPath->InitWithFile(path);
  proposedDBName.AppendLiteral(SUMMARY_SUFFIX);
  dbPath->Append(proposedDBName);
  PRBool exists;
  dbPath->Exists(&exists);
  if (exists)
  {
    dbPath->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 00600);
    dbPath->GetLeafName(proposedDBName);
  }
  // now, take the ".msf" off
  proposedDBName.SetLength(proposedDBName.Length() - NS_LITERAL_CSTRING(SUMMARY_SUFFIX).Length());
  dbPath->SetLeafName(proposedDBName);

  dbPath.swap(*dbFile);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetMsgDatabase(nsIMsgDatabase** aMsgDatabase)
{
  NS_ENSURE_ARG_POINTER(aMsgDatabase);
  GetDatabase();
  if (!mDatabase)
    return NS_ERROR_FAILURE;
  NS_ADDREF(*aMsgDatabase = mDatabase);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetMsgDatabase(nsIMsgDatabase *aMsgDatabase)
{
  if (mDatabase)
  {
    // commit here - db might go away when all these refs are released.
    mDatabase->Commit(nsMsgDBCommitType::kLargeCommit);
    mDatabase->RemoveListener(this);
    mDatabase->ClearCachedHdrs();
    if (!aMsgDatabase)
    {
      PRUint32 numNewKeys;
      PRUint32 *newMessageKeys;
      nsresult rv = mDatabase->GetNewList(&numNewKeys, &newMessageKeys);
      if (NS_SUCCEEDED(rv) && newMessageKeys)
      {
        m_newMsgs.Clear();
        m_newMsgs.AppendElements(newMessageKeys, numNewKeys);
      }
      NS_Free(newMessageKeys);
    }
  }
  mDatabase = aMsgDatabase;

  if (aMsgDatabase)
    aMsgDatabase->AddListener(this);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetBackupMsgDatabase(nsIMsgDatabase** aMsgDatabase)
{
  NS_ENSURE_ARG_POINTER(aMsgDatabase);
  nsresult rv = OpenBackupMsgDatabase();
  NS_ENSURE_SUCCESS(rv, rv);
  if (!mBackupDatabase)
    return NS_ERROR_FAILURE;

  NS_ADDREF(*aMsgDatabase = mBackupDatabase);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetDBFolderInfoAndDB(nsIDBFolderInfo **folderInfo, nsIMsgDatabase **database)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDBFolder::OnReadChanged(nsIDBChangeListener * aInstigator)
{
  /* do nothing.  if you care about this, override it.  see nsNewsFolder.cpp */
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::OnJunkScoreChanged(nsIDBChangeListener * aInstigator)
{
  NotifyFolderEvent(mJunkStatusChangedAtom);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::OnHdrPropertyChanged(nsIMsgDBHdr *aHdrToChange, PRBool aPreChange, PRUint32 *aStatus, 
                                   nsIDBChangeListener *aInstigator)
{
  /* do nothing.  if you care about this, override it.*/
  return NS_OK;
}

// 1.  When the status of a message changes.
NS_IMETHODIMP nsMsgDBFolder::OnHdrFlagsChanged(nsIMsgDBHdr *aHdrChanged, PRUint32 aOldFlags, PRUint32 aNewFlags,
                                         nsIDBChangeListener * aInstigator)
{
  if(aHdrChanged)
  {
    SendFlagNotifications(aHdrChanged, aOldFlags, aNewFlags);
    UpdateSummaryTotals(PR_TRUE);
  }

  // The old state was new message state
  // We check and see if this state has changed
  if(aOldFlags & nsMsgMessageFlags::New)
  {
    // state changing from new to something else
    if (!(aNewFlags  & nsMsgMessageFlags::New))
      CheckWithNewMessagesStatus(PR_FALSE);
  }

  return NS_OK;
}

nsresult nsMsgDBFolder::CheckWithNewMessagesStatus(PRBool messageAdded)
{
  nsresult rv;
  PRBool hasNewMessages;
  if (messageAdded)
    SetHasNewMessages(PR_TRUE);
  else // message modified or deleted
  {
    if(mDatabase)
    {
      rv = mDatabase->HasNew(&hasNewMessages);
      SetHasNewMessages(hasNewMessages);
    }
  }

  return NS_OK;
}

// 3.  When a message gets deleted, we need to see if it was new
//     When we lose a new message we need to check if there are still new messages
NS_IMETHODIMP nsMsgDBFolder::OnHdrDeleted(nsIMsgDBHdr *aHdrChanged, nsMsgKey  aParentKey, PRInt32 aFlags,
                          nsIDBChangeListener * aInstigator)
{
  // check to see if a new message is being deleted
  // as in this case, if there is only one new message and it's being deleted
  // the folder newness has to be cleared.
  CheckWithNewMessagesStatus(PR_FALSE);
  // Remove all processing flags.  This is generally a good thing although
  // undo-ing a message back into position will not re-gain the flags.
  nsMsgKey msgKey;
  aHdrChanged->GetMessageKey(&msgKey);
  AndProcessingFlags(msgKey, 0);
  return OnHdrAddedOrDeleted(aHdrChanged, PR_FALSE);
}

// 2.  When a new messages gets added, we need to see if it's new.
NS_IMETHODIMP nsMsgDBFolder::OnHdrAdded(nsIMsgDBHdr *aHdrChanged, nsMsgKey  aParentKey , PRInt32 aFlags,
                        nsIDBChangeListener * aInstigator)
{
  if(aFlags & nsMsgMessageFlags::New)
    CheckWithNewMessagesStatus(PR_TRUE);
  return OnHdrAddedOrDeleted(aHdrChanged, PR_TRUE);
}

nsresult nsMsgDBFolder::OnHdrAddedOrDeleted(nsIMsgDBHdr *aHdrChanged, PRBool added)
{
  if(added)
    NotifyItemAdded(aHdrChanged);
  else
    NotifyItemRemoved(aHdrChanged);
  UpdateSummaryTotals(PR_TRUE);
  return NS_OK;

}

NS_IMETHODIMP nsMsgDBFolder::OnParentChanged(nsMsgKey aKeyChanged, nsMsgKey oldParent, nsMsgKey newParent,
            nsIDBChangeListener * aInstigator)
{
  nsCOMPtr<nsIMsgDBHdr> hdrChanged;
  mDatabase->GetMsgHdrForKey(aKeyChanged, getter_AddRefs(hdrChanged));
  //In reality we probably want to just change the parent because otherwise we will lose things like
  //selection.
  if (hdrChanged)
  {
    //First delete the child from the old threadParent
    OnHdrAddedOrDeleted(hdrChanged, PR_FALSE);
    //Then add it to the new threadParent
    OnHdrAddedOrDeleted(hdrChanged, PR_TRUE);
  }
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::OnAnnouncerGoingAway(nsIDBChangeAnnouncer *instigator)
{
  if (mBackupDatabase && instigator == mBackupDatabase)
  {
    mBackupDatabase->RemoveListener(this);
    mBackupDatabase = nsnull;
  }
  else if (mDatabase)
  {
    mDatabase->RemoveListener(this);
    mDatabase = nsnull;
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::OnEvent(nsIMsgDatabase *aDB, const char *aEvent)
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetManyHeadersToDownload(PRBool *retval)
{
  NS_ENSURE_ARG_POINTER(retval);
  PRInt32 numTotalMessages;

  // is there any reason to return false?
  if (!mDatabase)
    *retval = PR_TRUE;
  else if (NS_SUCCEEDED(GetTotalMessages(PR_FALSE, &numTotalMessages)) && numTotalMessages <= 0)
    *retval = PR_TRUE;
  else
    *retval = PR_FALSE;
  return NS_OK;
}

nsresult nsMsgDBFolder::MsgFitsDownloadCriteria(nsMsgKey msgKey, PRBool *result)
{
  if(!mDatabase)
    return NS_ERROR_FAILURE;

  nsresult rv;
  nsCOMPtr<nsIMsgDBHdr> hdr;
  rv = mDatabase->GetMsgHdrForKey(msgKey, getter_AddRefs(hdr));
  if(NS_FAILED(rv))
    return rv;

  if (hdr)
  {
    PRUint32 msgFlags = 0;
    hdr->GetFlags(&msgFlags);
    // check if we already have this message body offline
    if (! (msgFlags & nsMsgMessageFlags::Offline))
    {
      *result = PR_TRUE;
      // check against the server download size limit .
      nsCOMPtr <nsIMsgIncomingServer> incomingServer;
      rv = GetServer(getter_AddRefs(incomingServer));
      if (NS_SUCCEEDED(rv) && incomingServer)
      {
        PRBool limitDownloadSize = PR_FALSE;
        rv = incomingServer->GetLimitOfflineMessageSize(&limitDownloadSize);
        NS_ENSURE_SUCCESS(rv, rv);
        if (limitDownloadSize)
        {
          PRInt32 maxDownloadMsgSize = 0;
          PRUint32 msgSize;
          hdr->GetMessageSize(&msgSize);
          rv = incomingServer->GetMaxMessageSize(&maxDownloadMsgSize);
          NS_ENSURE_SUCCESS(rv, rv);
          maxDownloadMsgSize *= 1024;
          if (msgSize > (PRUint32) maxDownloadMsgSize)
            *result = PR_FALSE;
        }
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetSupportsOffline(PRBool *aSupportsOffline)
{
  NS_ENSURE_ARG_POINTER(aSupportsOffline);
  if (mFlags & nsMsgFolderFlags::Virtual)
  {
    *aSupportsOffline = PR_FALSE;
    return NS_OK;
  }

  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv,rv);
  if (!server)
    return NS_ERROR_FAILURE;

  PRInt32 offlineSupportLevel;
  rv = server->GetOfflineSupportLevel(&offlineSupportLevel);
  NS_ENSURE_SUCCESS(rv,rv);

  *aSupportsOffline = (offlineSupportLevel >= OFFLINE_SUPPORT_LEVEL_REGULAR);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ShouldStoreMsgOffline(nsMsgKey msgKey, PRBool *result)
{
  NS_ENSURE_ARG(result);
  PRUint32 flags = 0;
  *result = PR_FALSE;
  GetFlags(&flags);
  return flags & nsMsgFolderFlags::Offline ? MsgFitsDownloadCriteria(msgKey, result) : NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::HasMsgOffline(nsMsgKey msgKey, PRBool *result)
{
  NS_ENSURE_ARG(result);
  *result = PR_FALSE;
  GetDatabase();
  if(!mDatabase)
    return NS_ERROR_FAILURE;

  nsresult rv;
  nsCOMPtr<nsIMsgDBHdr> hdr;
  rv = mDatabase->GetMsgHdrForKey(msgKey, getter_AddRefs(hdr));
  if(NS_FAILED(rv))
    return rv;

  if (hdr)
  {
    PRUint32 msgFlags = 0;
    hdr->GetFlags(&msgFlags);
    // check if we already have this message body offline
    if ((msgFlags & nsMsgMessageFlags::Offline))
      *result = PR_TRUE;
  }
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::GetFlags(PRUint32 *_retval)
{
  ReadDBFolderInfo(PR_FALSE);
  *_retval = mFlags;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ReadFromFolderCacheElem(nsIMsgFolderCacheElement *element)
{
  nsresult rv = NS_OK;
  nsCString charset;

  element->GetInt32Property("flags", (PRInt32 *) &mFlags);
  element->GetInt32Property("totalMsgs", &mNumTotalMessages);
  element->GetInt32Property("totalUnreadMsgs", &mNumUnreadMessages);
  element->GetInt32Property("pendingUnreadMsgs", &mNumPendingUnreadMessages);
  element->GetInt32Property("pendingMsgs", &mNumPendingTotalMessages);
  element->GetInt32Property("expungedBytes", (PRInt32 *) &mExpungedBytes);
  element->GetInt32Property("folderSize", (PRInt32 *) &mFolderSize);
  element->GetStringProperty("charset", mCharset);

#ifdef DEBUG_bienvenu1
  nsCString uri;
  GetURI(uri);
  printf("read total %ld for %s\n", mNumTotalMessages, uri.get());
#endif
  mInitializedFromCache = PR_TRUE;
  return rv;
}

nsresult nsMsgDBFolder::GetFolderCacheKey(nsILocalFile **aFile, PRBool createDBIfMissing /* = PR_FALSE */)
{
  nsresult rv;
  nsCOMPtr <nsILocalFile> path;
  rv = GetFilePath(getter_AddRefs(path));

  // now we put a new file  in aFile, because we're going to change it.
  nsCOMPtr <nsILocalFile> dbPath = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (dbPath)
  {
    dbPath->InitWithFile(path);
    // if not a server, we need to convert to a db Path with .msf on the end
    PRBool isServer = PR_FALSE;
    GetIsServer(&isServer);

    // if it's a server, we don't need the .msf appended to the name
    if (!isServer)
    {
      nsCOMPtr <nsILocalFile> summaryName;
      rv = GetSummaryFileLocation(dbPath, getter_AddRefs(summaryName));
      dbPath->InitWithFile(summaryName);

      // create the .msf file
      // see bug #244217 for details
      PRBool exists;
      if (createDBIfMissing && NS_SUCCEEDED(dbPath->Exists(&exists)) && !exists)
        dbPath->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
    }
  }
  NS_IF_ADDREF(*aFile = dbPath);
  return rv;
}

nsresult nsMsgDBFolder::FlushToFolderCache()
{
  nsresult rv;
  nsCOMPtr<nsIMsgAccountManager> accountManager =
           do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv) && accountManager)
  {
    nsCOMPtr<nsIMsgFolderCache> folderCache;
    rv = accountManager->GetFolderCache(getter_AddRefs(folderCache));
    if (NS_SUCCEEDED(rv) && folderCache)
      rv = WriteToFolderCache(folderCache, PR_FALSE);
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::WriteToFolderCache(nsIMsgFolderCache *folderCache, PRBool deep)
{
  nsresult rv = NS_OK;

  if (folderCache)
  {
    nsCOMPtr <nsIMsgFolderCacheElement> cacheElement;
    nsCOMPtr <nsILocalFile> dbPath;
    rv = GetFolderCacheKey(getter_AddRefs(dbPath));
#ifdef DEBUG_bienvenu1
    PRBool exists;
    NS_ASSERTION(NS_SUCCEEDED(dbPath->Exists(&exists)) && exists, "file spec we're adding to cache should exist");
#endif
    if (NS_SUCCEEDED(rv) && dbPath)
    {
      nsCString persistentPath;
      dbPath->GetPersistentDescriptor(persistentPath);
      rv = folderCache->GetCacheElement(persistentPath, PR_TRUE, getter_AddRefs(cacheElement));
      if (NS_SUCCEEDED(rv) && cacheElement)
        rv = WriteToFolderCacheElem(cacheElement);
    }
  }

  if (!deep)
    return rv;

  nsCOMPtr<nsISimpleEnumerator> enumerator;
  rv = GetSubFolders(getter_AddRefs(enumerator));
  if (NS_FAILED(rv))
    return rv;

  PRBool hasMore;
  while (NS_SUCCEEDED(enumerator->HasMoreElements(&hasMore)) && hasMore)
  {
    nsCOMPtr<nsISupports> item;
    enumerator->GetNext(getter_AddRefs(item));

    nsCOMPtr<nsIMsgFolder> msgFolder(do_QueryInterface(item));
    if (!msgFolder)
      continue;

    if (folderCache)
    {
      rv = msgFolder->WriteToFolderCache(folderCache, PR_TRUE);
      if (NS_FAILED(rv))
        break;
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::WriteToFolderCacheElem(nsIMsgFolderCacheElement *element)
{
  nsresult rv = NS_OK;

  element->SetInt32Property("flags", (PRInt32) mFlags);
  element->SetInt32Property("totalMsgs", mNumTotalMessages);
  element->SetInt32Property("totalUnreadMsgs", mNumUnreadMessages);
  element->SetInt32Property("pendingUnreadMsgs", mNumPendingUnreadMessages);
  element->SetInt32Property("pendingMsgs", mNumPendingTotalMessages);
  element->SetInt32Property("expungedBytes", mExpungedBytes);
  element->SetInt32Property("folderSize", mFolderSize);
  element->SetStringProperty("charset", mCharset);

#ifdef DEBUG_bienvenu1
  nsCString uri;
  GetURI(uri);
  printf("writing total %ld for %s\n", mNumTotalMessages, uri.get());
#endif
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::AddMessageDispositionState(nsIMsgDBHdr *aMessage, nsMsgDispositionState aDispositionFlag)
{
  NS_ENSURE_ARG_POINTER(aMessage);

  nsresult rv = GetDatabase();
  NS_ENSURE_SUCCESS(rv, NS_OK);

  nsMsgKey msgKey;
  aMessage->GetMessageKey(&msgKey);

  if (aDispositionFlag == nsIMsgFolder::nsMsgDispositionState_Replied)
    mDatabase->MarkReplied(msgKey, PR_TRUE, nsnull);
  else if (aDispositionFlag == nsIMsgFolder::nsMsgDispositionState_Forwarded)
    mDatabase->MarkForwarded(msgKey, PR_TRUE, nsnull);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::MarkAllMessagesRead(nsIMsgWindow *aMsgWindow)
{
  nsresult rv = GetDatabase();
  m_newMsgs.Clear();
  
  if (NS_SUCCEEDED(rv))
  {
    EnableNotifications(allMessageCountNotifications, PR_FALSE, PR_TRUE /*dbBatching*/);
    nsTArray<nsMsgKey> thoseMarked;
    rv = mDatabase->MarkAllRead(&thoseMarked);
    NS_ENSURE_SUCCESS(rv, rv);
    EnableNotifications(allMessageCountNotifications, PR_TRUE, PR_TRUE /*dbBatching*/);

    // Setup a undo-state
    if (aMsgWindow)
    {
      nsRefPtr<nsMsgReadStateTxn> readStateTxn = new nsMsgReadStateTxn();
      if (!readStateTxn)
        return NS_ERROR_OUT_OF_MEMORY;

      rv = readStateTxn->Init(this, thoseMarked);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = readStateTxn->SetTransactionType(nsIMessenger::eMarkAllMsg);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCOMPtr<nsITransactionManager> txnMgr;
      rv = aMsgWindow->GetTransactionManager(getter_AddRefs(txnMgr));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = txnMgr->DoTransaction(readStateTxn);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  SetHasNewMessages(PR_FALSE);
 
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::MarkThreadRead(nsIMsgThread *thread)
{
  nsresult rv = GetDatabase();
  if(NS_SUCCEEDED(rv))
    return mDatabase->MarkThreadRead(thread, nsnull, nsnull);
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::OnStartRunningUrl(nsIURI *aUrl)
{
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::OnStopRunningUrl(nsIURI *aUrl, nsresult aExitCode)
{
  NS_ENSURE_ARG_POINTER(aUrl);
  nsCOMPtr<nsIMsgMailNewsUrl> mailUrl = do_QueryInterface(aUrl);
  if (mailUrl)
  {
    PRBool updatingFolder = PR_FALSE;
    if (NS_SUCCEEDED(mailUrl->GetUpdatingFolder(&updatingFolder)) && updatingFolder)
      NotifyFolderEvent(mFolderLoadedAtom);
    // be sure to remove ourselves as a url listener
    mailUrl->UnRegisterListener(this);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetRetentionSettings(nsIMsgRetentionSettings **settings)
{
  NS_ENSURE_ARG_POINTER(settings);
  nsresult rv = NS_OK;
  if (!m_retentionSettings)
  {
    GetDatabase();
    if (mDatabase)
    {
      // get the settings from the db - if the settings from the db say the folder
      // is not overriding the incoming server settings, get the settings from the
      // server.
      rv = mDatabase->GetMsgRetentionSettings(getter_AddRefs(m_retentionSettings));
      if (NS_SUCCEEDED(rv) && m_retentionSettings)
      {
        PRBool useServerDefaults;
        m_retentionSettings->GetUseServerDefaults(&useServerDefaults);
        if (useServerDefaults)
        {
          nsCOMPtr <nsIMsgIncomingServer> incomingServer;
          rv = GetServer(getter_AddRefs(incomingServer));
          if (NS_SUCCEEDED(rv) && incomingServer)
            incomingServer->GetRetentionSettings(getter_AddRefs(m_retentionSettings));
        }
      }
    }
  }
  NS_IF_ADDREF(*settings = m_retentionSettings);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SetRetentionSettings(nsIMsgRetentionSettings *settings)
{
  m_retentionSettings = settings;
  GetDatabase();
  if (mDatabase)
    mDatabase->SetMsgRetentionSettings(settings);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetDownloadSettings(nsIMsgDownloadSettings **settings)
{
  NS_ENSURE_ARG_POINTER(settings);
  nsresult rv = NS_OK;
  if (!m_downloadSettings)
  {
    GetDatabase();
    if (mDatabase)
    {
      // get the settings from the db - if the settings from the db say the folder
      // is not overriding the incoming server settings, get the settings from the
      // server.
      rv = mDatabase->GetMsgDownloadSettings(getter_AddRefs(m_downloadSettings));
      if (NS_SUCCEEDED(rv) && m_downloadSettings)
      {
        PRBool useServerDefaults;
        m_downloadSettings->GetUseServerDefaults(&useServerDefaults);
        if (useServerDefaults)
        {
          nsCOMPtr <nsIMsgIncomingServer> incomingServer;
          rv = GetServer(getter_AddRefs(incomingServer));
          if (NS_SUCCEEDED(rv) && incomingServer)
            incomingServer->GetDownloadSettings(getter_AddRefs(m_downloadSettings));
        }
      }
    }
  }
  NS_IF_ADDREF(*settings = m_downloadSettings);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SetDownloadSettings(nsIMsgDownloadSettings *settings)
{
  m_downloadSettings = settings;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::IsCommandEnabled(const nsACString& command, PRBool *result)
{
  NS_ENSURE_ARG_POINTER(result);
  *result = PR_TRUE;
  return NS_OK;
}

nsresult nsMsgDBFolder::WriteStartOfNewLocalMessage()
{
  nsCAutoString result;
  char *ct;
  PRUint32 writeCount;
  time_t now = time ((time_t*) 0);
  ct = ctime(&now);
  ct[24] = 0;
  result = "From - ";
  result += ct;
  result += MSG_LINEBREAK;
  m_bytesAddedToLocalMsg = result.Length();

  nsCOMPtr <nsISeekableStream> seekable;
  PRInt64 curStorePos;

  if (m_offlineHeader)
    seekable = do_QueryInterface(m_tempMessageStream);

  if (seekable)
  {
    seekable->Tell(&curStorePos);
    m_offlineHeader->SetMessageOffset(curStorePos);
  }
  m_tempMessageStream->Write(result.get(), result.Length(),
                             &writeCount);
  if (seekable)
  {
    m_tempMessageStream->Flush();
    seekable->Tell(&curStorePos);
    m_offlineHeader->SetStatusOffset((PRUint32) curStorePos);
  }

  NS_NAMED_LITERAL_CSTRING(MozillaStatus, "X-Mozilla-Status: 0001" MSG_LINEBREAK);
  m_tempMessageStream->Write(MozillaStatus.get(), MozillaStatus.Length(),
                             &writeCount);
  m_bytesAddedToLocalMsg += writeCount;
  NS_NAMED_LITERAL_CSTRING(MozillaStatus2, "X-Mozilla-Status2: 00000000" MSG_LINEBREAK);
  m_bytesAddedToLocalMsg += MozillaStatus2.Length();
  return m_tempMessageStream->Write(MozillaStatus2.get(),
                                    MozillaStatus2.Length(), &writeCount);
}

nsresult nsMsgDBFolder::StartNewOfflineMessage()
{
  nsresult rv = NS_OK;
  if (!m_tempMessageStream)
    rv = GetOfflineStoreOutputStream(getter_AddRefs(m_tempMessageStream));
  else
  {
    nsCOMPtr <nsISeekableStream> seekable;
    seekable = do_QueryInterface(m_tempMessageStream);
    if (seekable)
      seekable->Seek(PR_SEEK_END, 0);
  }
  if (NS_SUCCEEDED(rv))
    WriteStartOfNewLocalMessage();
  m_numOfflineMsgLines = 0;
  return rv;
}

nsresult nsMsgDBFolder::EndNewOfflineMessage()
{
  nsCOMPtr <nsISeekableStream> seekable;
  PRInt64 curStorePos;
  PRUint64 messageOffset;
  PRUint32 messageSize;

  nsMsgKey messageKey;

  nsresult rv = GetDatabase();
  NS_ENSURE_SUCCESS(rv, rv);

  m_offlineHeader->GetMessageKey(&messageKey);
  if (m_tempMessageStream)
    seekable = do_QueryInterface(m_tempMessageStream);

  mDatabase->MarkOffline(messageKey, PR_TRUE, nsnull);
  if (seekable)
  {
    m_tempMessageStream->Flush();
    PRInt64 tellPos;
    seekable->Tell(&tellPos);
    curStorePos = tellPos;

    // N.B. This only works if we've set the offline flag for the message,
    // so be careful about moving the call to MarkOffline above.
    m_offlineHeader->GetMessageOffset(&messageOffset);
    curStorePos -= messageOffset;
    m_offlineHeader->SetOfflineMessageSize(curStorePos);
    m_offlineHeader->GetMessageSize(&messageSize);
    messageSize += m_bytesAddedToLocalMsg;
    // unix/mac has a one byte line ending, but the imap server returns
    // crlf terminated lines.
    if (MSG_LINEBREAK_LEN == 1)
      messageSize -= m_numOfflineMsgLines;

    // We clear the offline flag on the message if the size
    // looks wrong. Check if we're off by more than one byte per line.
    if (messageSize > (PRUint32) curStorePos &&
       (messageSize - (PRUint32) curStorePos) > (PRUint32) m_numOfflineMsgLines)
    {
       mDatabase->MarkOffline(messageKey, PR_FALSE, nsnull);
       NS_ERROR("offline message too small");
    }
    else
      m_offlineHeader->SetLineCount(m_numOfflineMsgLines);
  }
#ifdef _DEBUG
  nsCOMPtr<nsIInputStream> inputStream;
  GetOfflineStoreInputStream(getter_AddRefs(inputStream));
  if (inputStream)
    NS_ASSERTION(VerifyOfflineMessage(m_offlineHeader, inputStream),
                 "offline message doesn't start with From ");
#endif
  m_offlineHeader = nsnull;
  return NS_OK;
}

nsresult nsMsgDBFolder::CompactOfflineStore(nsIMsgWindow *inWindow, nsIUrlListener *aListener)
{
  nsresult rv;
  nsCOMPtr <nsIMsgFolderCompactor> folderCompactor =  do_CreateInstance(NS_MSGOFFLINESTORECOMPACTOR_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderCompactor->Compact(this, PR_TRUE, aListener, inWindow);
}

nsresult
nsMsgDBFolder::AutoCompact(nsIMsgWindow *aWindow)
{
  // we don't check for null aWindow, because this routine can get called
  // in unit tests where we have no window. Just assume not OK if no window.
  PRBool prompt;
  nsresult rv = GetPromptPurgeThreshold(&prompt);
  NS_ENSURE_SUCCESS(rv, rv);
  PRTime timeNow = PR_Now();   //time in microseconds
  PRTime timeAfterOneHourOfLastPurgeCheck;
  LL_ADD(timeAfterOneHourOfLastPurgeCheck, gtimeOfLastPurgeCheck, oneHour);
  if (LL_CMP(timeAfterOneHourOfLastPurgeCheck, <, timeNow) && prompt)
  {
   gtimeOfLastPurgeCheck = timeNow;
   nsCOMPtr<nsIMsgAccountManager> accountMgr = do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
   if (NS_SUCCEEDED(rv))
   {
     nsCOMPtr<nsISupportsArray> allServers;
     accountMgr->GetAllServers(getter_AddRefs(allServers));
     NS_ENSURE_SUCCESS(rv, rv);
     PRUint32 numServers, serverIndex=0;
     rv = allServers->Count(&numServers);
     PRInt32 offlineSupportLevel;
     if ( numServers > 0 )
     {
       nsCOMPtr<nsIMsgIncomingServer> server = do_QueryElementAt(allServers, serverIndex);
       NS_ENSURE_SUCCESS(rv, rv);
       nsCOMPtr<nsIMutableArray> folderArray = do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
       NS_ENSURE_SUCCESS(rv, rv);
       nsCOMPtr<nsIMutableArray> offlineFolderArray = do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
       NS_ENSURE_SUCCESS(rv, rv);
       PRInt32 totalExpungedBytes = 0;
       PRInt32 offlineExpungedBytes = 0;
       PRInt32 localExpungedBytes = 0;
       do
       {
         nsCOMPtr<nsIMsgFolder> rootFolder;
         rv = server->GetRootFolder(getter_AddRefs(rootFolder));
         if(NS_SUCCEEDED(rv) && rootFolder)
         {
           rv = server->GetOfflineSupportLevel(&offlineSupportLevel);
           NS_ENSURE_SUCCESS(rv, rv);
           nsCOMPtr<nsISupportsArray> allDescendents;
           NS_NewISupportsArray(getter_AddRefs(allDescendents));
           rootFolder->ListDescendents(allDescendents);
           PRUint32 cnt = 0;
           rv = allDescendents->Count(&cnt);
           NS_ENSURE_SUCCESS(rv, rv);
           PRUint32 expungedBytes=0;
           if (offlineSupportLevel > 0)
           {
             PRUint32 flags;
             for (PRUint32 i = 0; i < cnt; i++)
             {
               nsCOMPtr<nsIMsgFolder> folder = do_QueryElementAt(allDescendents, i);
               expungedBytes = 0;
               folder->GetFlags(&flags);
               if (flags & nsMsgFolderFlags::Offline)
                 folder->GetExpungedBytes(&expungedBytes);
               if (expungedBytes > 0 )
               {
                 offlineFolderArray->AppendElement(folder, PR_FALSE);
                 offlineExpungedBytes += expungedBytes;
               }
             }
           }
           else  //pop or local
           {
             for (PRUint32 i = 0; i < cnt; i++)
             {
               nsCOMPtr<nsIMsgFolder> folder = do_QueryElementAt(allDescendents, i);
               folder->GetExpungedBytes(&expungedBytes);
               if (expungedBytes > 0 )
               {
                 folderArray->AppendElement(folder, PR_FALSE);
                 localExpungedBytes += expungedBytes;
               }
             }
           }
         }
         server = do_QueryElementAt(allServers, ++serverIndex);
       }
       while (serverIndex < numServers);
       totalExpungedBytes = localExpungedBytes + offlineExpungedBytes;
       PRInt32 purgeThreshold;
       rv = GetPurgeThreshold(&purgeThreshold);
       NS_ENSURE_SUCCESS(rv, rv);
       if (totalExpungedBytes > (purgeThreshold * 1024))
       {
         PRBool okToCompact = PR_FALSE;
         nsCOMPtr<nsIPrefService> pref = do_GetService(NS_PREFSERVICE_CONTRACTID);
         nsCOMPtr<nsIPrefBranch> branch;
         pref->GetBranch("", getter_AddRefs(branch));

         PRBool askBeforePurge;
         branch->GetBoolPref(PREF_MAIL_PURGE_ASK, &askBeforePurge);
         if (askBeforePurge && aWindow)
         {
           nsCOMPtr <nsIStringBundle> bundle;
           rv = GetBaseStringBundle(getter_AddRefs(bundle));
           NS_ENSURE_SUCCESS(rv, rv);
           nsString dialogTitle;
           nsString confirmString;
           nsString checkboxText;
           nsString buttonCompactNowText;
           rv = bundle->GetStringFromName(NS_LITERAL_STRING("autoCompactAllFoldersTitle").get(), getter_Copies(dialogTitle));
           NS_ENSURE_SUCCESS(rv, rv);
           rv = bundle->GetStringFromName(NS_LITERAL_STRING("autoCompactAllFolders").get(), getter_Copies(confirmString));
           NS_ENSURE_SUCCESS(rv, rv);
           rv = bundle->GetStringFromName(NS_LITERAL_STRING("autoCompactAlwaysAskCheckbox").get(),
                                                            getter_Copies(checkboxText));
           NS_ENSURE_SUCCESS(rv, rv);
           rv = bundle->GetStringFromName(NS_LITERAL_STRING("compactNowButton").get(),
                                                            getter_Copies(buttonCompactNowText));
           NS_ENSURE_SUCCESS(rv, rv);
           PRBool alwaysAsk = PR_TRUE; // "Always ask..." - checked by default.
           PRInt32 buttonPressed = 0;

           nsCOMPtr<nsIPrompt> dialog;
           rv = aWindow->GetPromptDialog(getter_AddRefs(dialog));
           NS_ENSURE_SUCCESS(rv, rv);

          const PRUint32 buttonFlags =
            (nsIPrompt::BUTTON_TITLE_IS_STRING * nsIPrompt::BUTTON_POS_0) +
            (nsIPrompt::BUTTON_TITLE_CANCEL * nsIPrompt::BUTTON_POS_1);
           rv = dialog->ConfirmEx(dialogTitle.get(), confirmString.get(), buttonFlags,
                                  buttonCompactNowText.get(), nsnull, nsnull,
                                  checkboxText.get(), &alwaysAsk, &buttonPressed);
           NS_ENSURE_SUCCESS(rv, rv);
           if (!buttonPressed)
           {
             okToCompact = PR_TRUE;
             if (!alwaysAsk) // [ ] Always ask me before compacting folders automatically
               branch->SetBoolPref(PREF_MAIL_PURGE_ASK, PR_FALSE);
           }
         }
         else
           okToCompact = aWindow || !askBeforePurge;

         if (okToCompact)
         {
            nsCOMPtr <nsIAtom> aboutToCompactAtom = MsgGetAtom("AboutToCompact");
            NotifyFolderEvent(aboutToCompactAtom);

           if ( localExpungedBytes > 0)
           {
               nsCOMPtr<nsIMsgFolderCompactor> folderCompactor =
                 do_CreateInstance(NS_MSGLOCALFOLDERCOMPACTOR_CONTRACTID, &rv);
               NS_ENSURE_SUCCESS(rv, rv);

               if (offlineExpungedBytes > 0)
                 folderCompactor->CompactFolders(folderArray, offlineFolderArray, nsnull, aWindow);
               else
                 folderCompactor->CompactFolders(folderArray, nsnull, nsnull, aWindow);
           }
           else if (offlineExpungedBytes > 0)
             CompactAllOfflineStores(nsnull, aWindow, offlineFolderArray);
         }
       }
     }
   }
  }
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::CompactAllOfflineStores(nsIUrlListener *aUrlListener,
                                       nsIMsgWindow *aWindow,
                                       nsIArray *aOfflineFolderArray)
{
  nsresult rv;
  nsCOMPtr<nsIMsgFolderCompactor> folderCompactor
    = do_CreateInstance(NS_MSGOFFLINESTORECOMPACTOR_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderCompactor->CompactFolders(nsnull, aOfflineFolderArray, aUrlListener, aWindow);
}

nsresult
nsMsgDBFolder::GetPromptPurgeThreshold(PRBool *aPrompt)
{
  NS_ENSURE_ARG(aPrompt);
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv) && prefBranch)
  {
    rv = prefBranch->GetBoolPref(PREF_MAIL_PROMPT_PURGE_THRESHOLD, aPrompt);
    if (NS_FAILED(rv))
    {
      *aPrompt = PR_FALSE;
      rv = NS_OK;
    }
  }
  return rv;
}

nsresult
nsMsgDBFolder::GetPurgeThreshold(PRInt32 *aThreshold)
{
  NS_ENSURE_ARG(aThreshold);
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  if (NS_SUCCEEDED(rv) && prefBranch)
  {
    rv = prefBranch->GetIntPref(PREF_MAIL_PURGE_THRESHOLD, aThreshold);
    if (NS_FAILED(rv))
    {
      *aThreshold = 0;
      rv = NS_OK;
    }
  }
  return rv;
}

NS_IMETHODIMP //called on the folder that is renamed or about to be deleted
nsMsgDBFolder::MatchOrChangeFilterDestination(nsIMsgFolder *newFolder, PRBool caseInsensitive, PRBool *found)
{
  nsresult rv = NS_OK;
  nsCString oldUri;
  rv = GetURI(oldUri);
  NS_ENSURE_SUCCESS(rv,rv);

  nsCString newUri;
  if (newFolder) //for matching uri's this will be null
  {
    rv = newFolder->GetURI(newUri);
    NS_ENSURE_SUCCESS(rv,rv);
  }

  nsCOMPtr<nsIMsgFilterList> filterList;
  nsCOMPtr<nsIMsgAccountManager> accountMgr = do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsArray> allServers;
  rv = accountMgr->GetAllServers(getter_AddRefs(allServers));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 numServers;
  rv = allServers->Count(&numServers);
  for (PRUint32 serverIndex = 0; serverIndex < numServers; serverIndex++)
  {
    nsCOMPtr <nsIMsgIncomingServer> server = do_QueryElementAt(allServers, serverIndex);
    if (server)
    {
      PRBool canHaveFilters;
      rv = server->GetCanHaveFilters(&canHaveFilters);
      if (NS_SUCCEEDED(rv) && canHaveFilters)
      {
        // update the filterlist to match the new folder name
        rv = server->GetFilterList(nsnull, getter_AddRefs(filterList));
        if (NS_SUCCEEDED(rv) && filterList)
        {
          rv = filterList->MatchOrChangeFilterTarget(oldUri, newUri, caseInsensitive, found);
          if (NS_SUCCEEDED(rv) && found && newFolder && !newUri.IsEmpty())
            rv = filterList->SaveToDefaultFile();
        }
        // update the editable filterlist to match the new folder name
        rv = server->GetEditableFilterList(nsnull, getter_AddRefs(filterList));
        if (NS_SUCCEEDED(rv) && filterList)
        {
          rv = filterList->MatchOrChangeFilterTarget(oldUri, newUri, caseInsensitive, found);
          if (NS_SUCCEEDED(rv) && found && newFolder && !newUri.IsEmpty())
            rv = filterList->SaveToDefaultFile();
        }
      }
    }
  }
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::GetDBTransferInfo(nsIDBFolderInfo **aTransferInfo)
{
  nsCOMPtr <nsIDBFolderInfo> dbFolderInfo;
  nsCOMPtr <nsIMsgDatabase> db;
  GetDBFolderInfoAndDB(getter_AddRefs(dbFolderInfo), getter_AddRefs(db));
  if (dbFolderInfo)
    dbFolderInfo->GetTransferInfo(aTransferInfo);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetDBTransferInfo(nsIDBFolderInfo *aTransferInfo)
{
  NS_ENSURE_ARG(aTransferInfo);
  nsCOMPtr <nsIDBFolderInfo> dbFolderInfo;
  nsCOMPtr <nsIMsgDatabase> db;
  GetMsgDatabase(getter_AddRefs(db));
  if (db)
  {
    db->GetDBFolderInfo(getter_AddRefs(dbFolderInfo));
    if(dbFolderInfo)
      dbFolderInfo->InitFromTransferInfo(aTransferInfo);
    db->SetSummaryValid(PR_TRUE);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetStringProperty(const char *propertyName, nsACString& propertyValue)
{
  NS_ENSURE_ARG_POINTER(propertyName);
  nsCOMPtr <nsILocalFile> dbPath;
  nsresult rv = GetFolderCacheKey(getter_AddRefs(dbPath));
  if (dbPath)
  {
    nsCOMPtr <nsIMsgFolderCacheElement> cacheElement;
    rv = GetFolderCacheElemFromFile(dbPath, getter_AddRefs(cacheElement));
    if (cacheElement)  //try to get from cache
      rv = cacheElement->GetStringProperty(propertyName, propertyValue);
    if (NS_FAILED(rv))  //if failed, then try to get from db
    {
      nsCOMPtr<nsIDBFolderInfo> folderInfo;
      nsCOMPtr<nsIMsgDatabase> db;
      PRBool exists;
      rv = dbPath->Exists(&exists);
      if (NS_FAILED(rv) || !exists)
        return NS_MSG_ERROR_FOLDER_MISSING;
      rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
      if (NS_SUCCEEDED(rv))
        rv = folderInfo->GetCharProperty(propertyName, propertyValue);
    }
  }
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::SetStringProperty(const char *propertyName, const nsACString& propertyValue)
{
  NS_ENSURE_ARG_POINTER(propertyName);
  nsCOMPtr <nsILocalFile> dbPath;
  GetFolderCacheKey(getter_AddRefs(dbPath));
  if (dbPath)
  {
    nsCOMPtr <nsIMsgFolderCacheElement> cacheElement;
    GetFolderCacheElemFromFile(dbPath, getter_AddRefs(cacheElement));
    if (cacheElement)  //try to set in the cache
      cacheElement->SetStringProperty(propertyName, propertyValue);
  }
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  nsCOMPtr<nsIMsgDatabase> db;
  nsresult rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if(NS_SUCCEEDED(rv))
  {
    folderInfo->SetCharProperty(propertyName, propertyValue);
    db->Commit(nsMsgDBCommitType::kLargeCommit);  //commiting the db also commits the cache
  }
  return NS_OK;
}

// Get/Set ForcePropertyEmpty is only used with inherited properties
NS_IMETHODIMP
nsMsgDBFolder::GetForcePropertyEmpty(const char *aPropertyName, PRBool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  nsCAutoString nameEmpty(aPropertyName);
  nameEmpty.Append(NS_LITERAL_CSTRING(".empty"));
  nsCString value;
  GetStringProperty(nameEmpty.get(), value);
  *_retval = value.Equals(NS_LITERAL_CSTRING("true"));
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetForcePropertyEmpty(const char *aPropertyName, PRBool aValue)
{
 nsCAutoString nameEmpty(aPropertyName);
 nameEmpty.Append(NS_LITERAL_CSTRING(".empty"));
 return SetStringProperty(nameEmpty.get(),
   aValue ? NS_LITERAL_CSTRING("true") : NS_LITERAL_CSTRING(""));
}

NS_IMETHODIMP
nsMsgDBFolder::GetInheritedStringProperty(const char *aPropertyName, nsACString& aPropertyValue)
{
  NS_ENSURE_ARG_POINTER(aPropertyName);
  nsCString value;
  nsCOMPtr<nsIMsgIncomingServer> server;

  PRBool forceEmpty = PR_FALSE;

  if (!mIsServer)
  {
    GetForcePropertyEmpty(aPropertyName, &forceEmpty);
  }
  else
  {
    // root folders must get their values from the server
    GetServer(getter_AddRefs(server));
    if (server)
      server->GetForcePropertyEmpty(aPropertyName, &forceEmpty);
  }

  if (forceEmpty)
  {
    aPropertyValue.Truncate();
    return NS_OK;
  }

  // servers will automatically inherit from the preference mail.server.default.(propertyName)
  if (server)
    return server->GetCharValue(aPropertyName, aPropertyValue);

  GetStringProperty(aPropertyName, value);
  if (value.IsEmpty())
  {
    // inherit from the parent
    nsCOMPtr<nsIMsgFolder> parent;
    GetParent(getter_AddRefs(parent));
    if (parent)
      return parent->GetInheritedStringProperty(aPropertyName, aPropertyValue);
  }

  aPropertyValue.Assign(value);
  return NS_OK;
}

nsresult
nsMsgDBFolder::SpamFilterClassifyMessage(const char *aURI, nsIMsgWindow *aMsgWindow, nsIJunkMailPlugin *aJunkMailPlugin)
{
  nsresult rv;
  nsCOMPtr<nsIMsgTraitService> traitService(do_GetService("@mozilla.org/msg-trait-service;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 count;
  PRUint32 *proIndices;
  PRUint32 *antiIndices;
  rv = traitService->GetEnabledIndices(&count, &proIndices, &antiIndices);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aJunkMailPlugin->ClassifyTraitsInMessage(aURI, count, proIndices, antiIndices, this, aMsgWindow, this);
  NS_Free(proIndices);
  NS_Free(antiIndices);
  return rv;
}

nsresult
nsMsgDBFolder::SpamFilterClassifyMessages(const char **aURIArray, PRUint32 aURICount, nsIMsgWindow *aMsgWindow, nsIJunkMailPlugin *aJunkMailPlugin)
{

  nsresult rv;
  nsCOMPtr<nsIMsgTraitService> traitService(do_GetService("@mozilla.org/msg-trait-service;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 count;
  PRUint32 *proIndices;
  PRUint32 *antiIndices;
  rv = traitService->GetEnabledIndices(&count, &proIndices, &antiIndices);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aJunkMailPlugin->ClassifyTraitsInMessages(aURICount, aURIArray, count,
      proIndices, antiIndices, this, aMsgWindow, this);
  NS_Free(proIndices);
  NS_Free(antiIndices);
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::OnMessageClassified(const char *aMsgURI,
                                   nsMsgJunkStatus aClassification,
                                   PRUint32 aJunkPercent)
{
  if (!aMsgURI) // This signifies end of batch.
  {
    nsresult rv = NS_OK;
    // Apply filters if needed.
    PRUint32 length;
    if (mPostBayesMessagesToFilter &&
         NS_SUCCEEDED(mPostBayesMessagesToFilter->GetLength(&length)) &&
         length)
    {
      // Apply post-bayes filtering.
      nsCOMPtr<nsIMsgFilterService>
        filterService(do_GetService(NS_MSGFILTERSERVICE_CONTRACTID, &rv));
      if (NS_SUCCEEDED(rv))
        // We use a null nsIMsgWindow because we don't want some sort of ui
        // appearing in the middle of automatic filtering (plus I really don't
        // want to propagate that value.)
        rv = filterService->ApplyFilters(nsMsgFilterType::PostPlugin,
                                         mPostBayesMessagesToFilter,
                                         this, nsnull /* nsIMsgWindow */);
      mPostBayesMessagesToFilter->Clear();
    }

    // Bail if we didn't actually classify any messages.
    if (mClassifiedMsgKeys.IsEmpty())
      return rv;

    // Notify that we classified some messages.
    nsCOMPtr<nsIMsgFolderNotificationService>
      notifier(do_GetService(NS_MSGNOTIFICATIONSERVICE_CONTRACTID, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr <nsIMutableArray> classifiedMsgHdrs =
      do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 numKeys = mClassifiedMsgKeys.Length();
    for (PRUint32 i = 0 ; i < numKeys ; ++i)
    {
      nsMsgKey msgKey = mClassifiedMsgKeys[i];
      PRBool hasKey;
      // It is very possible for a message header to no longer be around because
      // a filter moved it.
      rv = mDatabase->ContainsKey(msgKey, &hasKey);
      if (!NS_SUCCEEDED(rv) || !hasKey)
        continue;
      nsCOMPtr <nsIMsgDBHdr> msgHdr;
      rv = mDatabase->GetMsgHdrForKey(msgKey, getter_AddRefs(msgHdr));
      if (!NS_SUCCEEDED(rv))
        continue;
      classifiedMsgHdrs->AppendElement(msgHdr, PR_FALSE);
    }

    // only generate the notification if there are some classified messages
    if (NS_SUCCEEDED(classifiedMsgHdrs->GetLength(&length)) && length)
      notifier->NotifyMsgsClassified(classifiedMsgHdrs,
                                     mBayesJunkClassifying,
                                     mBayesTraitClassifying);
    mClassifiedMsgKeys.Clear();

    return rv;
  }

  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISpamSettings> spamSettings;
  rv = server->GetSpamSettings(getter_AddRefs(spamSettings));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr <nsIMsgDBHdr> msgHdr;
  rv = GetMsgDBHdrFromURI(aMsgURI, getter_AddRefs(msgHdr));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgKey msgKey;
  rv = msgHdr->GetMessageKey(&msgKey);
  NS_ENSURE_SUCCESS(rv, rv);

  // check if this message needs junk classification
  PRUint32 processingFlags;
  GetProcessingFlags(msgKey, &processingFlags);

  if (processingFlags & nsMsgProcessingFlags::ClassifyJunk)
  {
    mClassifiedMsgKeys.AppendElement(msgKey);
    AndProcessingFlags(msgKey, ~nsMsgProcessingFlags::ClassifyJunk);

    nsCAutoString msgJunkScore;
    msgJunkScore.AppendInt(aClassification == nsIJunkMailPlugin::JUNK ?
          nsIJunkMailPlugin::IS_SPAM_SCORE:
          nsIJunkMailPlugin::IS_HAM_SCORE);
    mDatabase->SetStringProperty(msgKey, "junkscore", msgJunkScore.get());
    mDatabase->SetStringProperty(msgKey, "junkscoreorigin", "plugin");

    nsCAutoString strPercent;
    strPercent.AppendInt(aJunkPercent);
    mDatabase->SetStringProperty(msgKey, "junkpercent", strPercent.get());

    if (aClassification == nsIJunkMailPlugin::JUNK)
    {
      // IMAP has its own way of marking read.
      if (!(mFlags & nsMsgFolderFlags::ImapBox))
      {
        PRBool markAsReadOnSpam;
        (void)spamSettings->GetMarkAsReadOnSpam(&markAsReadOnSpam);
        if (markAsReadOnSpam)
        {
          rv = mDatabase->MarkRead(msgKey, true, this);
          if (!NS_SUCCEEDED(rv))
            NS_WARNING("failed marking spam message as read");
        }
      }
      // mail folders will log junk hits with move info. Perhaps we should
      // add a log here for non-mail folders as well, that don't override
      // onMessageClassified
      //rv = spamSettings->LogJunkHit(msgHdr, PR_FALSE);
      //NS_ENSURE_SUCCESS(rv,rv);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::OnMessageTraitsClassified(const char *aMsgURI,
                                         PRUint32 aTraitCount,
                                         PRUint32 *aTraits,
                                         PRUint32 *aPercents)
{
  if (!aMsgURI) // This signifies end of batch
    return NS_OK; // We are not handling batching
  
  nsresult rv;
  nsCOMPtr <nsIMsgDBHdr> msgHdr;
  rv = GetMsgDBHdrFromURI(aMsgURI, getter_AddRefs(msgHdr));
  NS_ENSURE_SUCCESS(rv, rv);

  nsMsgKey msgKey;
  rv = msgHdr->GetMessageKey(&msgKey);
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 processingFlags;
  GetProcessingFlags(msgKey, &processingFlags);
  if (!(processingFlags & nsMsgProcessingFlags::ClassifyTraits))
    return NS_OK;

  AndProcessingFlags(msgKey, ~nsMsgProcessingFlags::ClassifyTraits);

  nsCOMPtr<nsIMsgTraitService> traitService;
  traitService = do_GetService("@mozilla.org/msg-trait-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  for (PRUint32 i = 0; i < aTraitCount; i++)
  {
    if (aTraits[i] == nsIJunkMailPlugin::JUNK_TRAIT)
      continue; // junk is processed by the junk listener
    nsCAutoString traitId;
    rv = traitService->GetId(aTraits[i], traitId);
    traitId.Insert(NS_LITERAL_CSTRING("bayespercent/"), 0);
    nsCAutoString strPercent;
    strPercent.AppendInt(aPercents[i]);
    mDatabase->SetStringPropertyByHdr(msgHdr, traitId.get(), strPercent.get());
  }
  return NS_OK;
}

/**
 * Call the filter plugins (XXX currently just one)
 */
NS_IMETHODIMP
nsMsgDBFolder::CallFilterPlugins(nsIMsgWindow *aMsgWindow, PRBool *aFiltersRun)
{
  NS_ENSURE_ARG_POINTER(aFiltersRun);
  *aFiltersRun = PR_FALSE;
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsCOMPtr<nsISpamSettings> spamSettings;
  PRInt32 spamLevel = 0;

  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString serverType;
  server->GetType(serverType);

  rv = server->GetSpamSettings(getter_AddRefs(spamSettings));
  nsCOMPtr <nsIMsgFilterPlugin> filterPlugin;
  server->GetSpamFilterPlugin(getter_AddRefs(filterPlugin));
  if (!filterPlugin) // it's not an error not to have the filter plugin.
    return NS_OK;
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr <nsIJunkMailPlugin> junkMailPlugin = do_QueryInterface(filterPlugin);
  if (!junkMailPlugin) // we currently only support the junk mail plugin
    return NS_OK;

  // if it's a news folder, then we really don't support junk in the ui
  // yet the legacy spamLevel seems to think we should analyze it.
  // Maybe we should upgrade that, but for now let's not analyze. We'll
  // let an extension set an inherited property if they really want us to
  // analyze this. We need that anyway to allow extension-based overrides.
  // When we finalize adding junk in news to core, we'll deal with the
  // spamLevel issue

  // if this is the junk folder, or the trash folder
  // don't analyze for spam, because we don't care
  //
  // if it's the sent, unsent, templates, or drafts,
  // don't analyze for spam, because the user
  // created that message
  //
  // if it's a public imap folder, or another users
  // imap folder, don't analyze for spam, because
  // it's not ours to analyze
  //

  PRBool filterForJunk = PR_TRUE;
  if (serverType.EqualsLiteral("rss") ||
      (mFlags & (nsMsgFolderFlags::Junk | nsMsgFolderFlags::Trash |
                 nsMsgFolderFlags::SentMail | nsMsgFolderFlags::Queue |
                 nsMsgFolderFlags::Drafts | nsMsgFolderFlags::Templates |
                 nsMsgFolderFlags::ImapPublic | nsMsgFolderFlags::Newsgroup |
                 nsMsgFolderFlags::ImapOtherUser) &&
       !(mFlags & nsMsgFolderFlags::Inbox)))
    filterForJunk = PR_FALSE;

  spamSettings->GetLevel(&spamLevel);
  if (!spamLevel)
    filterForJunk = PR_FALSE;

  /*
   * We'll use inherited folder properties for the junk trait to override the
   * standard server-based activation of junk processing. This provides a
   * hook for extensions to customize the application of junk filtering.
   * Set inherited property "dobayes.mailnews@mozilla.org#junk" to "true"
   * to force junk processing, and "false" to skip junk processing.
   */

  nsCAutoString junkEnableOverride;
  GetInheritedStringProperty("dobayes.mailnews@mozilla.org#junk", junkEnableOverride);
  if (junkEnableOverride.EqualsLiteral("true"))
    filterForJunk = PR_TRUE;
  else if (junkEnableOverride.EqualsLiteral("false"))
    filterForJunk = PR_FALSE;

  PRBool userHasClassified = PR_FALSE;
  // if the user has not classified any messages yet, then we shouldn't bother
  // running the junk mail controls. This creates a better first use experience.
  // See Bug #250084.
  junkMailPlugin->GetUserHasClassified(&userHasClassified);
  if (!userHasClassified)
    filterForJunk = PR_FALSE;

  if (!mDatabase)
  {
    rv = GetDatabase();
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(mDatabase, NS_ERROR_NOT_AVAILABLE);
  }

  // check if trait processing needed

  nsCOMPtr<nsIMsgTraitService> traitService(
      do_GetService("@mozilla.org/msg-trait-service;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  PRUint32 count = 0, *proIndices, *antiIndices;
  rv = traitService->GetEnabledIndices(&count, &proIndices, &antiIndices);
  PRBool filterForOther = PR_FALSE;
  if (NS_SUCCEEDED(rv)) // We just skip this on failure, since it is rarely used
  {
    for (PRUint32 i = 0; i < count; ++i)
    {
      // The trait service determines which traits are globally enabled or
      // disabled. If a trait is enabled, it can still be made inactive
      // on a particular folder using an inherited property. To do that,
      // set "dobayes." + trait proID as an inherited folder property with
      // the string value "false"
      //
      // If any non-junk traits are active on the folder, then the bayes
      // processing will calculate probabilities for all enabled traits.

      if (proIndices[i] != nsIJunkMailPlugin::JUNK_TRAIT)
      {
        filterForOther = PR_TRUE;
        nsCAutoString traitId;
        nsCAutoString property("dobayes.");
        traitService->GetId(proIndices[i], traitId);
        property.Append(traitId);
        nsCAutoString isEnabledOnFolder;
        GetInheritedStringProperty(property.get(), isEnabledOnFolder);
        if (isEnabledOnFolder.EqualsLiteral("false"))
          filterForOther = PR_FALSE;
        // We might have to allow a "true" override in the future, but
        // for now there is no way for that to affect the processing
        break;
      }
    }
    NS_Free(proIndices);
    NS_Free(antiIndices);
  }

  // Do we need to apply message filters?
  PRBool filterPostPlugin = PR_FALSE; // Do we have a post-analysis filter?
  nsCOMPtr<nsIMsgFilterList> filterList;
  GetFilterList(aMsgWindow, getter_AddRefs(filterList));
  if (filterList)
  {
    PRUint32 filterCount = 0;
    filterList->GetFilterCount(&filterCount);
    for (PRUint32 index = 0; index < filterCount && !filterPostPlugin; ++index)
    {
      nsCOMPtr<nsIMsgFilter> filter;
      filterList->GetFilterAt(index, getter_AddRefs(filter));
      if (!filter)
        continue;
      nsMsgFilterTypeType filterType;
      filter->GetFilterType(&filterType);
      if (!(filterType & nsMsgFilterType::PostPlugin))
        continue;
      PRBool enabled = PR_FALSE;
      filter->GetEnabled(&enabled);
      if (!enabled)
        continue;
      filterPostPlugin = PR_TRUE;
    }
  }

  // If there is nothing to do, leave now but let NotifyHdrsNotBeingClassified
  // generate the msgsClassified notification for all newly added messages as
  // tracked by the NotReportedClassified processing flag.
  if (!filterForOther && !filterForJunk && !filterPostPlugin)
  {
    NotifyHdrsNotBeingClassified();
    return NS_OK;
  }

  // get the list of new messages
  //
  PRUint32 numNewKeys;
  PRUint32 *newKeys;
  rv = mDatabase->GetNewList(&numNewKeys, &newKeys);
  NS_ENSURE_SUCCESS(rv, rv);

  nsTArray<nsMsgKey> newMessageKeys;
  // Start from m_saveNewMsgs (and clear its current state).  m_saveNewMsgs is
  // where we stash the list of new messages when we are told to clear the list
  // of new messages by the UI (which purges the list from the nsMsgDatabase).
  newMessageKeys.SwapElements(m_saveNewMsgs);
  if (numNewKeys)
    newMessageKeys.AppendElements(newKeys, numNewKeys);

  NS_Free(newKeys);

  // build up list of keys to classify
  nsTArray<nsMsgKey> classifyMsgKeys;
  nsCString uri;

  PRUint32 numNewMessages = newMessageKeys.Length();
  for (PRUint32 i = 0 ; i < numNewMessages ; ++i)
  {
    nsCOMPtr <nsIMsgDBHdr> msgHdr;
    nsMsgKey msgKey = newMessageKeys[i];
    rv = mDatabase->GetMsgHdrForKey(msgKey, getter_AddRefs(msgHdr));
    if (!NS_SUCCEEDED(rv))
      continue;
    // per-message junk tests.
    PRBool filterMessageForJunk = PR_FALSE;
    while (filterForJunk)  // we'll break from this at the end
    {
      nsCString junkScore;
      msgHdr->GetStringProperty("junkscore", getter_Copies(junkScore));
      if (!junkScore.IsEmpty()) // ignore already scored messages.
        break;

      PRBool whiteListMessage = PR_FALSE;
      spamSettings->CheckWhiteList(msgHdr, &whiteListMessage);
      if (whiteListMessage)
      {
        // mark this msg as non-junk, because we whitelisted it.

        nsCAutoString msgJunkScore;
        msgJunkScore.AppendInt(nsIJunkMailPlugin::IS_HAM_SCORE);
        mDatabase->SetStringProperty(msgKey, "junkscore", msgJunkScore.get());
        mDatabase->SetStringProperty(msgKey, "junkscoreorigin", "whitelist");
        break; // skip this msg since it's in the white list
      }
      filterMessageForJunk = PR_TRUE;

      OrProcessingFlags(msgKey, nsMsgProcessingFlags::ClassifyJunk);
      // Since we are junk processing, we want to defer the msgsClassified
      // notification until the junk classification has occurred.  The event
      // is sufficiently reliable that we know this will be handled in
      // OnMessageClassified at the end of the batch.  We clear the
      // NotReportedClassified flag since we know the message is in good hands.
      AndProcessingFlags(msgKey, ~nsMsgProcessingFlags::NotReportedClassified);
      break;
    }

    PRUint32 processingFlags;
    GetProcessingFlags(msgKey, &processingFlags);

    PRBool filterMessageForOther = PR_FALSE;
    // trait processing
    if (!(processingFlags & nsMsgProcessingFlags::TraitsDone))
    {
      // don't do trait processing on this message again
      OrProcessingFlags(msgKey, nsMsgProcessingFlags::TraitsDone);
      if (filterForOther)
      {
        filterMessageForOther = PR_TRUE;
        OrProcessingFlags(msgKey, nsMsgProcessingFlags::ClassifyTraits);
      }
    }

    if (filterMessageForJunk || filterMessageForOther)
      classifyMsgKeys.AppendElement(newMessageKeys[i]);

    // Set messages to filter post-bayes.
    // Have we already filtered this message?
    if (!(processingFlags & nsMsgProcessingFlags::FiltersDone))
    {
      if (filterPostPlugin)
      {
        // Don't do filters on this message again.
        // (Only set this if we are actually filtering since this is
        // tantamount to a memory leak.)
        OrProcessingFlags(msgKey, nsMsgProcessingFlags::FiltersDone);
        // Lazily create the array.
        if (!mPostBayesMessagesToFilter)
          mPostBayesMessagesToFilter = do_CreateInstance(NS_ARRAY_CONTRACTID);
        mPostBayesMessagesToFilter->AppendElement(msgHdr, PR_FALSE);
      }
    }
  }

  NotifyHdrsNotBeingClassified();
  // If there weren't any new messages, just return.
  if (newMessageKeys.IsEmpty())
    return NS_OK;

  // If we do not need to do any work, leave.
  // (We needed to get the list of new messages so we could get their headers so
  // we can send notifications about them here.)

  if (!classifyMsgKeys.IsEmpty())
  {
    // Remember what classifications are the source of this decision for when
    // we perform the notification in OnMessageClassified at the conclusion of
    // classification.
    mBayesJunkClassifying = filterForJunk;
    mBayesTraitClassifying = filterForOther;

    PRUint32 numMessagesToClassify = classifyMsgKeys.Length();
    char ** messageURIs = (char **) PR_MALLOC(sizeof(const char *) * numMessagesToClassify);
    if (!messageURIs)
      return NS_ERROR_OUT_OF_MEMORY;

    for (PRUint32 msgIndex = 0; msgIndex < numMessagesToClassify ; ++msgIndex )
    {
      nsCString tmpStr;
      rv = GenerateMessageURI(classifyMsgKeys[msgIndex], tmpStr);
      messageURIs[msgIndex] = ToNewCString(tmpStr);
      if (NS_FAILED(rv))
          NS_WARNING("nsMsgDBFolder::CallFilterPlugins(): could not"
                     " generate URI for message");
    }
    // filterMsgs
    *aFiltersRun = PR_TRUE;
    rv = SpamFilterClassifyMessages((const char **) messageURIs, numMessagesToClassify, aMsgWindow, junkMailPlugin);
    for ( PRUint32 freeIndex=0 ; freeIndex < numMessagesToClassify ; ++freeIndex )
      PR_Free(messageURIs[freeIndex]);
    PR_Free(messageURIs);
  }
  else if (filterPostPlugin)
  {
    // Nothing to classify, so need to end batch ourselves. We do this so that
    // post analysis filters will run consistently on a folder, even if
    // disabled junk processing, which could be dynamic through whitelisting,
    // makes the bayes analysis unnecessary.
    OnMessageClassified(nsnull, nsnull, nsnull);
  }

  return rv;
}

/**
 * Adds the messages in the NotReportedClassified mProcessing set to the
 * (possibly empty) array of msgHdrsNotBeingClassified, and send the
 * nsIMsgFolderNotificationService notification.
 */
nsresult nsMsgDBFolder::NotifyHdrsNotBeingClassified()
{
  nsCOMPtr<nsIMutableArray> msgHdrsNotBeingClassified;

  if (mProcessingFlag[5].keys)
  {
    nsTArray<nsMsgKey> keys;
    mProcessingFlag[5].keys->ToMsgKeyArray(keys);
    if (keys.Length())
    {
      msgHdrsNotBeingClassified = do_CreateInstance(NS_ARRAY_CONTRACTID);
      if (!msgHdrsNotBeingClassified)
        return NS_ERROR_OUT_OF_MEMORY;
      MsgGetHeadersFromKeys(mDatabase, keys, msgHdrsNotBeingClassified);

      // Since we know we've handled all the NotReportedClassified messages,
      // we clear the set by deleting and recreating it.
      delete mProcessingFlag[5].keys;
      mProcessingFlag[5].keys = nsMsgKeySetU::Create();
      nsCOMPtr<nsIMsgFolderNotificationService>
        notifier(do_GetService(NS_MSGNOTIFICATIONSERVICE_CONTRACTID));
      if (notifier)
        notifier->NotifyMsgsClassified(msgHdrsNotBeingClassified,
                                       // no classification is being performed
                                       PR_FALSE, PR_FALSE);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetLastMessageLoaded(nsMsgKey *aMsgKey)
{
  NS_ENSURE_ARG_POINTER(aMsgKey);
  *aMsgKey = mLastMessageLoaded;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetLastMessageLoaded(nsMsgKey aMsgKey)
{
  mLastMessageLoaded = aMsgKey;
  return NS_OK;
}

// Returns true if: a) there is no need to prompt or b) the user is already
// logged in or c) the user logged in successfully.
PRBool nsMsgDBFolder::PromptForMasterPasswordIfNecessary()
{
  nsresult rv;
  nsCOMPtr<nsIMsgAccountManager> accountManager =
    do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  PRBool userNeedsToAuthenticate = PR_FALSE;
  // if we're PasswordProtectLocalCache, then we need to find out if the server
  // is authenticated.
  (void) accountManager->GetUserNeedsToAuthenticate(&userNeedsToAuthenticate);
  if (!userNeedsToAuthenticate)
    return PR_TRUE;

  // Do we have a master password?
  nsCOMPtr<nsIPK11TokenDB> tokenDB =
    do_GetService(NS_PK11TOKENDB_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  nsCOMPtr<nsIPK11Token> token;
  rv = tokenDB->GetInternalKeyToken(getter_AddRefs(token));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  PRBool result;
  rv = token->CheckPassword(EmptyString().get(), &result);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  if (result)
  {
    // We don't have a master password, so this function isn't supported,
    // therefore just tell account manager we've authenticated and return true.
    accountManager->SetUserNeedsToAuthenticate(PR_FALSE);
    return PR_TRUE;
  }

  // We have a master password, so try and login to the slot.
  rv = token->Login(PR_FALSE);
  if (NS_FAILED(rv))
    // Login failed, so we didn't get a password (e.g. prompt cancelled).
    return PR_FALSE;

  // Double-check that we are now logged in
  rv = token->IsLoggedIn(&result);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  accountManager->SetUserNeedsToAuthenticate(!result);
  return result;
}

// this gets called after the last junk mail classification has run.
nsresult nsMsgDBFolder::PerformBiffNotifications(void)
{
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  PRInt32  numBiffMsgs = 0;
  nsCOMPtr<nsIMsgFolder> root;
  rv = GetRootFolder(getter_AddRefs(root));
  root->GetNumNewMessages(PR_TRUE, &numBiffMsgs);
  if (numBiffMsgs > 0)
  {
    server->SetPerformingBiff(true);
    SetBiffState(nsIMsgFolder::nsMsgBiffState_NewMail);
    server->SetPerformingBiff(false);
  }
  return NS_OK;
}

nsresult
nsMsgDBFolder::initializeStrings()
{
  nsresult rv;
  nsCOMPtr<nsIStringBundleService> bundleService =
      do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIStringBundle> bundle;
  rv = bundleService->CreateBundle("chrome://messenger/locale/messenger.properties",
                                   getter_AddRefs(bundle));
  NS_ENSURE_SUCCESS(rv, rv);

  bundle->GetStringFromName(NS_LITERAL_STRING("inboxFolderName").get(),
                            &kLocalizedInboxName);
  bundle->GetStringFromName(NS_LITERAL_STRING("trashFolderName").get(),
                            &kLocalizedTrashName);
  bundle->GetStringFromName(NS_LITERAL_STRING("sentFolderName").get(),
                            &kLocalizedSentName);
  bundle->GetStringFromName(NS_LITERAL_STRING("draftsFolderName").get(),
                            &kLocalizedDraftsName);
  bundle->GetStringFromName(NS_LITERAL_STRING("templatesFolderName").get(),
                            &kLocalizedTemplatesName);
  bundle->GetStringFromName(NS_LITERAL_STRING("junkFolderName").get(),
                            &kLocalizedJunkName);
  bundle->GetStringFromName(NS_LITERAL_STRING("outboxFolderName").get(),
                            &kLocalizedUnsentName);
  bundle->GetStringFromName(NS_LITERAL_STRING("archivesFolderName").get(),
                            &kLocalizedArchivesName);

  nsCOMPtr<nsIStringBundle> brandBundle;
  rv = bundleService->CreateBundle("chrome://branding/locale/brand.properties", getter_AddRefs(bundle));
  NS_ENSURE_SUCCESS(rv, rv);
  bundle->GetStringFromName(NS_LITERAL_STRING("brandShortName").get(),
                            &kLocalizedBrandShortName);
  return NS_OK;
}

nsresult
nsMsgDBFolder::createCollationKeyGenerator()
{
  nsresult rv = NS_OK;

  nsCOMPtr<nsILocaleService> localeSvc = do_GetService(NS_LOCALESERVICE_CONTRACTID,&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocale> locale;
  rv = localeSvc->GetApplicationLocale(getter_AddRefs(locale));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr <nsICollationFactory> factory = do_CreateInstance(NS_COLLATIONFACTORY_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = factory->CreateCollation(locale, &gCollationKeyGenerator);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::Init(const char* aURI)
{
  // for now, just initialize everything during Init()
  nsresult rv;
  rv = nsRDFResource::Init(aURI);
  NS_ENSURE_SUCCESS(rv, rv);
  return CreateBaseMessageURI(nsDependentCString(aURI));
}

nsresult nsMsgDBFolder::CreateBaseMessageURI(const nsACString& aURI)
{
  // Each folder needs to implement this.
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetURI(nsACString& name)
{
  return nsRDFResource::GetValueUTF8(name);
}

////////////////////////////////////////////////////////////////////////////////
#if 0
typedef PRBool
(*nsArrayFilter)(nsISupports* element, void* data);
#endif
////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
nsMsgDBFolder::GetSubFolders(nsISimpleEnumerator **aResult)
{
  return aResult ? NS_NewArrayEnumerator(aResult, mSubFolders) : NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsMsgDBFolder::FindSubFolder(const nsACString& aEscapedSubFolderName, nsIMsgFolder **aFolder)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIRDFService> rdf(do_GetService(kRDFServiceCID, &rv));

  if (NS_FAILED(rv))
    return rv;

  // XXX use necko here
  nsCAutoString uri;
  uri.Append(mURI);
  uri.Append('/');
  uri.Append(aEscapedSubFolderName);

  nsCOMPtr<nsIRDFResource> res;
  rv = rdf->GetResource(uri, getter_AddRefs(res));
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIMsgFolder> folder(do_QueryInterface(res, &rv));
  if (NS_FAILED(rv))
    return rv;

  folder.swap(*aFolder);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetHasSubFolders(PRBool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = mSubFolders.Count() > 0;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetNumSubFolders(PRUint32 *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = mSubFolders.Count();
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::AddFolderListener(nsIFolderListener * listener)
{
  return mListeners.AppendElement(listener) ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

NS_IMETHODIMP nsMsgDBFolder::RemoveFolderListener(nsIFolderListener * listener)
{
  mListeners.RemoveElement(listener);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetParent(nsIMsgFolder *aParent)
{
  mParent = do_GetWeakReference(aParent);
  if (aParent)
  {
    nsresult rv;
    nsCOMPtr<nsIMsgFolder> parentMsgFolder = do_QueryInterface(aParent, &rv);
    if (NS_SUCCEEDED(rv))
    {
      // servers do not have parents, so we must not be a server
      mIsServer = PR_FALSE;
      mIsServerIsValid = PR_TRUE;

      // also set the server itself while we're here.
      nsCOMPtr<nsIMsgIncomingServer> server;
      rv = parentMsgFolder->GetServer(getter_AddRefs(server));
      if (NS_SUCCEEDED(rv) && server)
        mServer = do_GetWeakReference(server);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetParent(nsIMsgFolder **aParent)
{
  NS_ENSURE_ARG_POINTER(aParent);
  nsCOMPtr<nsIMsgFolder> parent = do_QueryReferent(mParent);
  parent.swap(*aParent);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetMessages(nsISimpleEnumerator **result)
{
  // XXX should this return an empty enumeration?
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsMsgDBFolder::UpdateFolder(nsIMsgWindow *)
{
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP nsMsgDBFolder::GetFolderURL(nsACString& url)
{
  url.Assign(EmptyCString());
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetServer(nsIMsgIncomingServer ** aServer)
{
  NS_ENSURE_ARG_POINTER(aServer);
  nsresult rv;
  // short circut the server if we have it.
  nsCOMPtr<nsIMsgIncomingServer> server = do_QueryReferent(mServer, &rv);
  if (NS_FAILED(rv))
  {
    // try again after parsing the URI
    rv = parseURI(PR_TRUE);
    server = do_QueryReferent(mServer);
  }
  server.swap(*aServer);
  return *aServer ? NS_OK : NS_ERROR_FAILURE;
}

#ifdef MSG_FASTER_URI_PARSING
class nsMsgAutoBool {
public:
  nsMsgAutoBool() : mValue(nsnull) {}
  void autoReset(PRBool *aValue) { mValue = aValue; }
  ~nsMsgAutoBool() { if (mValue) *mValue = PR_FALSE; }
private:
  PRBool *mValue;
};
#endif

nsresult
nsMsgDBFolder::parseURI(PRBool needServer)
{
  nsresult rv;
  nsCOMPtr<nsIURL> url;

#ifdef MSG_FASTER_URI_PARSING
  nsMsgAutoBool parsingUrlState;
  if (mParsingURLInUse)
    url = do_CreateInstance(NS_STANDARDURL_CONTRACTID, &rv);
  else
  {
    url = mParsingURL;
    mParsingURLInUse = PR_TRUE;
    parsingUrlState.autoReset(&mParsingURLInUse);
  }
#else
  url = do_CreateInstance(NS_STANDARDURL_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  rv = url->SetSpec(mURI);
  NS_ENSURE_SUCCESS(rv, rv);
  // empty path tells us it's a server.
  if (!mIsServerIsValid)
  {
    nsCAutoString path;
    rv = url->GetPath(path);
    if (NS_SUCCEEDED(rv))
      mIsServer = path.EqualsLiteral("/");
    mIsServerIsValid = PR_TRUE;
  }

  // grab the name off the leaf of the server
  if (mName.IsEmpty())
  {
    // mName:
    // the name is the trailing directory in the path
    nsCAutoString fileName;
    nsCAutoString escapedFileName;
    url->GetFileName(escapedFileName);
    if (!escapedFileName.IsEmpty())
    {
      // XXX conversion to unicode here? is fileName in UTF8?
      // yes, let's say it is in utf8
      MsgUnescapeString(escapedFileName, 0, fileName);
      NS_ASSERTION(MsgIsUTF8(fileName), "fileName is not in UTF-8");
      CopyUTF8toUTF16(fileName, mName);
    }
  }

  // grab the server by parsing the URI and looking it up
  // in the account manager...
  // But avoid this extra work by first asking the parent, if any
  nsCOMPtr<nsIMsgIncomingServer> server = do_QueryReferent(mServer, &rv);
  if (NS_FAILED(rv))
  {
    // first try asking the parent instead of the URI
    nsCOMPtr<nsIMsgFolder> parentMsgFolder;
    GetParent(getter_AddRefs(parentMsgFolder));

    if (parentMsgFolder)
      rv = parentMsgFolder->GetServer(getter_AddRefs(server));

    // no parent. do the extra work of asking
    if (!server && needServer)
    {
      nsCOMPtr<nsIMsgAccountManager> accountManager =
               do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      nsCString serverType;
      GetIncomingServerType(serverType);
      url->SetScheme(serverType);
      rv = accountManager->FindServerByURI(url, PR_FALSE,
                                      getter_AddRefs(server));
      NS_ENSURE_SUCCESS(rv, rv);
    }
    mServer = do_GetWeakReference(server);
  } /* !mServer */

  // now try to find the local path for this folder
  if (server)
  {
    nsCAutoString newPath;
    nsCAutoString escapedUrlPath;
    nsCAutoString urlPath;
    url->GetFilePath(escapedUrlPath);
    if (!escapedUrlPath.IsEmpty())
    {
      MsgUnescapeString(escapedUrlPath, 0, urlPath);

      // transform the filepath from the URI, such as
      // "/folder1/folder2/foldern"
      // to
      // "folder1.sbd/folder2.sbd/foldern"
      // (remove leading / and add .sbd to first n-1 folders)
      // to be appended onto the server's path
      PRBool isNewsFolder = PR_FALSE;
      nsCAutoString scheme;
      if (NS_SUCCEEDED(url->GetScheme(scheme)))
      {
        isNewsFolder = scheme.EqualsLiteral("news") ||
                       scheme.EqualsLiteral("snews") ||
                       scheme.EqualsLiteral("nntp");
      }
      NS_MsgCreatePathStringFromFolderURI(urlPath.get(), newPath, scheme,
                                          isNewsFolder);
    }

    // now append munged path onto server path
    nsCOMPtr<nsILocalFile> serverPath;
    rv = server->GetLocalPath(getter_AddRefs(serverPath));
    if (NS_FAILED(rv)) return rv;

    if (!mPath && serverPath)
    {
      if (!newPath.IsEmpty())
      {
        // I hope this is temporary - Ultimately,
        // NS_MsgCreatePathStringFromFolderURI will need to be fixed.
#if defined(XP_WIN) || defined(XP_OS2)
        MsgReplaceChar(newPath, '/', '\\');
#endif
        rv = serverPath->AppendRelativeNativePath(newPath);
        NS_ASSERTION(NS_SUCCEEDED(rv),"failed to append to the serverPath");
        if (NS_FAILED(rv))
        {
          mPath = nsnull;
          return rv;
        }
      }
      mPath = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      mPath->InitWithFile(serverPath);
    }
    // URI is completely parsed when we've attempted to get the server
    mHaveParsedURI=PR_TRUE;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetIsServer(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  // make sure we've parsed the URI
  if (!mIsServerIsValid)
  {
    nsresult rv = parseURI();
    if (NS_FAILED(rv) || !mIsServerIsValid)
      return NS_ERROR_FAILURE;
  }

  *aResult = mIsServer;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetNoSelect(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetImapShared(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  return GetFlag(nsMsgFolderFlags::PersonalShared, aResult);
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanSubscribe(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  // by default, you can't subscribe.
  // if otherwise, override it.
  *aResult = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanFileMessages(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  //varada - checking folder flag to see if it is the "Unsent Messages"
  //and if so return FALSE
  if (mFlags & (nsMsgFolderFlags::Queue | nsMsgFolderFlags::Virtual))
  {
    *aResult = PR_FALSE;
    return NS_OK;
  }

  PRBool isServer = PR_FALSE;
  nsresult rv = GetIsServer(&isServer);
  if (NS_FAILED(rv)) return rv;

  // by default, you can't file messages into servers, only to folders
  // if otherwise, override it.
  *aResult = !isServer;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanDeleteMessages(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanCreateSubfolders(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  //Checking folder flag to see if it is the "Unsent Messages"
  //or a virtual folder, and if so return FALSE
  if (mFlags & (nsMsgFolderFlags::Queue | nsMsgFolderFlags::Virtual))
  {
    *aResult = PR_FALSE;
    return NS_OK;
  }

  // by default, you can create subfolders on server and folders
  // if otherwise, override it.
  *aResult = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanRename(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  PRBool isServer = PR_FALSE;
  nsresult rv = GetIsServer(&isServer);
  if (NS_FAILED(rv)) return rv;
  // by default, you can't rename servers, only folders
  // if otherwise, override it.
  //
  // check if the folder is a special folder
  // (Trash, Drafts, Unsent Messages, Inbox, Sent, Templates, Junk, Archives)
  // if it is, don't allow the user to rename it
  // (which includes dnd moving it with in the same server)
  //
  // this errors on the side of caution.  we'll return false a lot
  // more often if we use flags,
  // instead of checking if the folder really is being used as a
  // special folder by looking at the "copies and folders" prefs on the
  // identities.
  *aResult = !(isServer || (mFlags & nsMsgFolderFlags::Trash ||
           mFlags & nsMsgFolderFlags::Drafts ||
           mFlags & nsMsgFolderFlags::Queue ||
           mFlags & nsMsgFolderFlags::Inbox ||
           mFlags & nsMsgFolderFlags::SentMail ||
           mFlags & nsMsgFolderFlags::Templates ||
           mFlags & nsMsgFolderFlags::Archive ||
           mFlags & nsMsgFolderFlags::Junk));
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetCanCompact(PRBool *aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  PRBool isServer = PR_FALSE;
  nsresult rv = GetIsServer(&isServer);
  NS_ENSURE_SUCCESS(rv,rv);
  // servers cannot be compacted --> 4.x
  // virtual search folders cannot be compacted
  *aResult = !isServer && !(mFlags & nsMsgFolderFlags::Virtual);
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::GetPrettyName(nsAString& name)
{
  return GetName(name);
}

NS_IMETHODIMP nsMsgDBFolder::SetPrettyName(const nsAString& name)
{
  nsresult rv;

  //Set pretty name only if special flag is set and if it the default folder name
  if (mFlags & nsMsgFolderFlags::Inbox && name.LowerCaseEqualsLiteral("inbox"))
    rv = SetName(nsDependentString(kLocalizedInboxName));
  else if (mFlags & nsMsgFolderFlags::SentMail && name.LowerCaseEqualsLiteral("sent"))
    rv = SetName(nsDependentString(kLocalizedSentName));
  else if (mFlags & nsMsgFolderFlags::Drafts && name.LowerCaseEqualsLiteral("drafts"))
    rv = SetName(nsDependentString(kLocalizedDraftsName));
  else if (mFlags & nsMsgFolderFlags::Templates && name.LowerCaseEqualsLiteral("templates"))
    rv = SetName(nsDependentString(kLocalizedTemplatesName));
  else if (mFlags & nsMsgFolderFlags::Trash && name.LowerCaseEqualsLiteral("trash"))
    rv = SetName(nsDependentString(kLocalizedTrashName));
  else if (mFlags & nsMsgFolderFlags::Queue && name.LowerCaseEqualsLiteral("unsent messages"))
    rv = SetName(nsDependentString(kLocalizedUnsentName));
  else if (mFlags & nsMsgFolderFlags::Junk && name.LowerCaseEqualsLiteral("junk"))
    rv = SetName(nsDependentString(kLocalizedJunkName));
  else if (mFlags & nsMsgFolderFlags::Archive && name.LowerCaseEqualsLiteral("archives"))
    rv = SetName(nsDependentString(kLocalizedArchivesName));
  else
    rv = SetName(name);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetName(nsAString& name)
{
  nsresult rv;
  if (!mHaveParsedURI && mName.IsEmpty())
  {
    rv = parseURI();
    if (NS_FAILED(rv)) return rv;
  }

  // if it's a server, just forward the call
  if (mIsServer)
  {
    nsCOMPtr<nsIMsgIncomingServer> server;
    rv = GetServer(getter_AddRefs(server));
    if (NS_SUCCEEDED(rv) && server)
      return server->GetPrettyName(name);
  }

  name = mName;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetName(const nsAString& name)
{
  // override the URI-generated name
  if (!mName.Equals(name))
  {
    mName = name;
    // old/new value doesn't matter here
    NotifyUnicharPropertyChanged(kNameAtom, name, name);
  }
  return NS_OK;
}

//For default, just return name
NS_IMETHODIMP nsMsgDBFolder::GetAbbreviatedName(nsAString& aAbbreviatedName)
{
  return GetName(aAbbreviatedName);
}

NS_IMETHODIMP
nsMsgDBFolder::GetChildNamed(const nsAString& aName, nsIMsgFolder **aChild)
{
  NS_ENSURE_ARG_POINTER(aChild);
  GetSubFolders(nsnull); // initialize mSubFolders
  *aChild = nsnull;
  PRInt32 count = mSubFolders.Count();

  for (PRInt32 i = 0; i < count; i++)
  {
    nsString folderName;
    nsresult rv = mSubFolders[i]->GetName(folderName);
    // case-insensitive compare is probably LCD across OS filesystems
    if (NS_SUCCEEDED(rv) &&
        folderName.Equals(aName, nsCaseInsensitiveStringComparator()))
    {
      NS_ADDREF(*aChild = mSubFolders[i]);
      return NS_OK;
    }
  }
  // don't return NS_OK if we didn't find the folder
  // see http://bugzilla.mozilla.org/show_bug.cgi?id=210089#c15
  // and http://bugzilla.mozilla.org/show_bug.cgi?id=210089#c17
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsMsgDBFolder::GetChildWithURI(const nsACString& uri, PRBool deep, PRBool caseInsensitive, nsIMsgFolder ** child)
{
  NS_ENSURE_ARG_POINTER(child);
  // will return nsnull if we can't find it
  *child = nsnull;
  nsCOMPtr<nsISimpleEnumerator> enumerator;
  nsresult rv = GetSubFolders(getter_AddRefs(enumerator));
  if (NS_FAILED(rv))
    return rv;

  PRBool hasMore;
  while(NS_SUCCEEDED(enumerator->HasMoreElements(&hasMore)) && hasMore)
  {
    nsCOMPtr<nsISupports> item;
    enumerator->GetNext(getter_AddRefs(item));

    nsCOMPtr<nsIRDFResource> folderResource(do_QueryInterface(item));
    nsCOMPtr<nsIMsgFolder> folder(do_QueryInterface(item));
    if (folderResource && folder)
    {
      const char *folderURI;
      rv = folderResource->GetValueConst(&folderURI);
      if (NS_FAILED(rv)) return rv;
      PRBool equal = folderURI && (caseInsensitive ? uri.Equals(folderURI, nsCaseInsensitiveCStringComparator())
                                                   : uri.Equals(folderURI));
      if (equal)
      {
        *child = folder;
        NS_ADDREF(*child);
        return NS_OK;
      }
      if (deep)
      {
        rv = folder->GetChildWithURI(uri, deep, caseInsensitive, child);
        if (NS_FAILED(rv))
          return rv;

        if (*child)
          return NS_OK;
      }
    }
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetPrettiestName(nsAString& name)
{
  if (NS_SUCCEEDED(GetPrettyName(name)))
    return NS_OK;
  return GetName(name);
}


NS_IMETHODIMP nsMsgDBFolder::GetShowDeletedMessages(PRBool *showDeletedMessages)
{
  NS_ENSURE_ARG_POINTER(showDeletedMessages);
  *showDeletedMessages = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::Delete()
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::DeleteSubFolders(nsIArray *folders,
                                              nsIMsgWindow *msgWindow)
{
  PRUint32 count;
  nsresult rv = folders->GetLength(&count);
  for(PRUint32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgFolder> folder(do_QueryElementAt(folders, i, &rv));
    if (folder)
      PropagateDelete(folder, PR_TRUE, msgWindow);
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::CreateStorageIfMissing(nsIUrlListener* /* urlListener */)
{
  NS_ASSERTION(PR_FALSE, "needs to be overridden");
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::PropagateDelete(nsIMsgFolder *folder, PRBool deleteStorage, nsIMsgWindow *msgWindow)
{
  // first, find the folder we're looking to delete
  nsresult rv = NS_OK;

  PRInt32 count = mSubFolders.Count();
  for (PRInt32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgFolder> child(mSubFolders[i]);
    if (folder == child.get())
    {
      // Remove self as parent
      child->SetParent(nsnull);
      // maybe delete disk storage for it, and its subfolders
      rv = child->RecursiveDelete(deleteStorage, msgWindow);
      if (NS_SUCCEEDED(rv))
      {
        // Remove from list of subfolders.
        mSubFolders.RemoveObjectAt(i);
        NotifyItemRemoved(child);
        break;
      }
      else // setting parent back if we failed
        child->SetParent(this);
    }
    else
      rv = child->PropagateDelete(folder, deleteStorage, msgWindow);
  }

  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::RecursiveDelete(PRBool deleteStorage, nsIMsgWindow *msgWindow)
{
  // If deleteStorage is PR_TRUE, recursively deletes disk storage for this folder
  // and all its subfolders.
  // Regardless of deleteStorage, always unlinks them from the children lists and
  // frees memory for the subfolders but NOT for _this_

  nsresult status = NS_OK;
  nsCOMPtr <nsILocalFile> dbPath;

  // first remove the deleted folder from the folder cache;
  nsresult result = GetFolderCacheKey(getter_AddRefs(dbPath));

  nsCOMPtr<nsIMsgAccountManager> accountMgr =
    do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &result);
  if(NS_SUCCEEDED(result))
  {
    nsCOMPtr <nsIMsgFolderCache> folderCache;
    result = accountMgr->GetFolderCache(getter_AddRefs(folderCache));
    if (NS_SUCCEEDED(result) && folderCache)
    {
      nsCString persistentPath;
      dbPath->GetPersistentDescriptor(persistentPath);
      folderCache->RemoveElement(persistentPath);
    }
  }

  PRInt32 count = mSubFolders.Count();
  while (count > 0)
  {
    nsIMsgFolder *child = mSubFolders[0];

    child->SetParent(nsnull);
    status = child->RecursiveDelete(deleteStorage, msgWindow);  // recur
    if (NS_SUCCEEDED(status))
      // unlink it from this child's list
      mSubFolders.RemoveObjectAt(0);
    else
    {
      // setting parent back if we failed for some reason
      child->SetParent(this);
      break;
    }

    count--;
  }

  // now delete the disk storage for _this_
  if (deleteStorage && status == NS_OK)
  {
    // All delete commands use deleteStorage = true, and local moves use false.
    // IMAP moves use true, leaving this here in the hope that bug 439108
    // works out.
    nsCOMPtr<nsIMsgFolderNotificationService> notifier(do_GetService(NS_MSGNOTIFICATIONSERVICE_CONTRACTID));
    if (notifier)
      notifier->NotifyFolderDeleted(this);
    status = Delete();
  }
  return status;
}

NS_IMETHODIMP nsMsgDBFolder::CreateSubfolder(const nsAString& folderName, nsIMsgWindow *msgWindow)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::AddSubfolder(const nsAString& name,
                                          nsIMsgFolder** child)
{
  NS_ENSURE_ARG_POINTER(child);

  PRInt32 flags = 0;
  nsresult rv;
  nsCOMPtr<nsIRDFService> rdf = do_GetService("@mozilla.org/rdf/rdf-service;1", &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  nsCAutoString uri(mURI);
  uri.Append('/');

  // URI should use UTF-8
  // (see RFC2396 Uniform Resource Identifiers (URI): Generic Syntax)
  nsCAutoString escapedName;
  rv = NS_MsgEscapeEncodeURLPath(name, escapedName);
  NS_ENSURE_SUCCESS(rv, rv);

  // fix for #192780
  // if this is the root folder
  // make sure the the special folders
  // have the right uri.
  // on disk, host\INBOX should be a folder with the uri mailbox://user@host/Inbox"
  // as mailbox://user@host/Inbox != mailbox://user@host/INBOX
  nsCOMPtr<nsIMsgFolder> rootFolder;
  rv = GetRootFolder(getter_AddRefs(rootFolder));
  if (NS_SUCCEEDED(rv) && rootFolder && (rootFolder.get() == (nsIMsgFolder *)this))
  {
    if (MsgLowerCaseEqualsLiteral(escapedName, "inbox"))
      uri += "Inbox";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "unsent%20messages"))
      uri += "Unsent%20Messages";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "drafts"))
      uri += "Drafts";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "trash"))
      uri += "Trash";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "sent"))
      uri += "Sent";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "templates"))
      uri +="Templates";
    else if (MsgLowerCaseEqualsLiteral(escapedName, "archives"))
      uri += "Archives";
    else
      uri += escapedName.get();
  }
  else
    uri += escapedName.get();

  nsCOMPtr <nsIMsgFolder> msgFolder;
  rv = GetChildWithURI(uri, PR_FALSE/*deep*/, PR_TRUE /*case Insensitive*/, getter_AddRefs(msgFolder));
  if (NS_SUCCEEDED(rv) && msgFolder)
    return NS_MSG_FOLDER_EXISTS;

  nsCOMPtr<nsIRDFResource> res;
  rv = rdf->GetResource(uri, getter_AddRefs(res));
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr<nsIMsgFolder> folder(do_QueryInterface(res, &rv));
  if (NS_FAILED(rv))
    return rv;

  nsCOMPtr <nsILocalFile> path;
  // we just need to do this for the parent folder, i.e., "this".
  rv = CreateDirectoryForFolder(getter_AddRefs(path));
  NS_ENSURE_SUCCESS(rv, rv);

  folder->GetFlags((PRUint32 *)&flags);
  flags |= nsMsgFolderFlags::Mail;
  folder->SetParent(this);

  PRBool isServer;
  rv = GetIsServer(&isServer);

  //Only set these if these are top level children.
  if(NS_SUCCEEDED(rv) && isServer)
  {
    if(name.LowerCaseEqualsLiteral("inbox"))
    {
      flags |= nsMsgFolderFlags::Inbox;
      SetBiffState(nsIMsgFolder::nsMsgBiffState_Unknown);
    }
    else if (name.LowerCaseEqualsLiteral("trash"))
      flags |= nsMsgFolderFlags::Trash;
    else if (name.LowerCaseEqualsLiteral("unsent messages") ||
      name.LowerCaseEqualsLiteral("outbox"))
      flags |= nsMsgFolderFlags::Queue;
#if 0
    // the logic for this has been moved into
    // SetFlagsOnDefaultMailboxes()
    else if(name.EqualsIgnoreCase(NS_LITERAL_STRING("Sent"), nsCaseInsensitiveStringComparator()))
      folder->SetFlag(nsMsgFolderFlags::SentMail);
    else if(name.EqualsIgnoreCase(NS_LITERAL_STRING("Drafts"), nsCaseInsensitiveStringComparator()))
      folder->SetFlag(nsMsgFolderFlags::Drafts);
    else if(name.EqualsIgnoreCase(NS_LITERAL_STRING("Templates"), nsCaseInsensitiveStringComparator()))
      folder->SetFlag(nsMsgFolderFlags::Templates);
#endif
  }

  folder->SetFlags(flags);

  if (folder)
    mSubFolders.AppendObject(folder);

  folder.swap(*child);
  // at this point we must be ok and we don't want to return failure in case
  // GetIsServer failed.
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::Compact(nsIUrlListener *aListener, nsIMsgWindow *aMsgWindow)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::CompactAll(nsIUrlListener *aListener, nsIMsgWindow *aMsgWindow, PRBool aCompactOfflineAlso)
{
  NS_ASSERTION(PR_FALSE, "should be overridden by child class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::EmptyTrash(nsIMsgWindow *msgWindow, nsIUrlListener *aListener)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult
nsMsgDBFolder::CheckIfFolderExists(const nsAString& newFolderName, nsIMsgFolder *parentFolder, nsIMsgWindow *msgWindow)
{
  NS_ENSURE_ARG_POINTER(parentFolder);
  nsCOMPtr<nsISimpleEnumerator> subFolders;
  nsresult rv = parentFolder->GetSubFolders(getter_AddRefs(subFolders));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasMore;
  while (NS_SUCCEEDED(subFolders->HasMoreElements(&hasMore)) && hasMore)
  {
    nsCOMPtr<nsISupports> item;
    rv = subFolders->GetNext(getter_AddRefs(item));

    nsCOMPtr<nsIMsgFolder> msgFolder(do_QueryInterface(item));
    if (!msgFolder)
      break;

    nsString folderName;

    msgFolder->GetName(folderName);
    if (folderName.Equals(newFolderName, nsCaseInsensitiveStringComparator()))
    {
      ThrowAlertMsg("folderExists", msgWindow);
      return NS_MSG_FOLDER_EXISTS;
    }
  }
  return NS_OK;
}


nsresult
nsMsgDBFolder::AddDirectorySeparator(nsILocalFile *path)
{
  nsAutoString leafName;
  path->GetLeafName(leafName);
  leafName.AppendLiteral(FOLDER_SUFFIX);
  return path->SetLeafName(leafName);
}

/* Finds the directory associated with this folder.  That is if the path is
   c:\Inbox, it will return c:\Inbox.sbd if it succeeds.  If that path doesn't
   currently exist then it will create it. Path is strictly an out parameter.
  */
nsresult nsMsgDBFolder::CreateDirectoryForFolder(nsILocalFile **resultFile)
{
  nsresult rv = NS_OK;

  nsCOMPtr<nsILocalFile> path;
  rv = GetFilePath(getter_AddRefs(path));
  if (NS_FAILED(rv)) return rv;

  PRBool pathIsDirectory = PR_FALSE;
  path->IsDirectory(&pathIsDirectory);
  if(!pathIsDirectory)
  {
    //If the current path isn't a directory, add directory separator
    //and test it out.
    rv = AddDirectorySeparator(path);
    if(NS_FAILED(rv))
      return rv;

    //If that doesn't exist, then we have to create this directory
    pathIsDirectory = PR_FALSE;
    path->IsDirectory(&pathIsDirectory);
    if(!pathIsDirectory)
    {
      PRBool pathExists;
      path->Exists(&pathExists);
      //If for some reason there's a file with the directory separator
      //then we are going to fail.
      rv = pathExists ? NS_MSG_COULD_NOT_CREATE_DIRECTORY : path->Create(nsIFile::DIRECTORY_TYPE, 0700);
    }
  }
  if (NS_SUCCEEDED(rv))
    path.swap(*resultFile);
  return rv;
}

/* Finds the backup directory associated with this folder, stored on the temp
   drive. If that path doesn't currently exist then it will create it. Path is
   strictly an out parameter.
  */
nsresult nsMsgDBFolder::CreateBackupDirectory(nsILocalFile **resultFile)
{
  nsCOMPtr<nsIFile> pathIFile;
  nsresult rv = NS_GetSpecialDirectory(NS_OS_TEMP_DIR,
                                       getter_AddRefs(pathIFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> path = do_QueryInterface(pathIFile, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = path->Append(NS_LITERAL_STRING("MozillaMailnews"));
  PRBool pathIsDirectory;
  path->IsDirectory(&pathIsDirectory);

  // If that doesn't exist, then we have to create this directory
  if (!pathIsDirectory)
  {
    PRBool pathExists;
    path->Exists(&pathExists);
    // If for some reason there's a file with the directory separator
    // then we are going to fail.
    rv = pathExists ? NS_MSG_COULD_NOT_CREATE_DIRECTORY :
                      path->Create(nsIFile::DIRECTORY_TYPE, 0700);
  }
  if (NS_SUCCEEDED(rv))
    path.swap(*resultFile);
  return rv;
}

nsresult nsMsgDBFolder::GetBackupSummaryFile(nsILocalFile **aBackupFile, const nsACString& newName)
{
  nsCOMPtr<nsILocalFile> backupDir;
  nsresult rv = CreateBackupDirectory(getter_AddRefs(backupDir));
  NS_ENSURE_SUCCESS(rv, rv);

  // We use a dummy message folder file so we can use
  // GetSummaryFileLocation to get the db file name
  nsCOMPtr<nsILocalFile> backupDBDummyFolder;
  rv = CreateBackupDirectory(getter_AddRefs(backupDBDummyFolder));
  NS_ENSURE_SUCCESS(rv, rv);

  if (!newName.IsEmpty())
  {
    rv = backupDBDummyFolder->AppendNative(newName);
  }
  else // if newName is null, use the folder name
  {
    nsCOMPtr<nsILocalFile> folderPath;
    rv = GetFilePath(getter_AddRefs(folderPath));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCAutoString folderName;
    rv = folderPath->GetNativeLeafName(folderName);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = backupDBDummyFolder->AppendNative(folderName);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> backupDBFile;
  rv = GetSummaryFileLocation(backupDBDummyFolder, getter_AddRefs(backupDBFile));
  NS_ENSURE_SUCCESS(rv, rv);

  backupDBFile.swap(*aBackupFile);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::Rename(const nsAString& aNewName, nsIMsgWindow *msgWindow)
{
  nsCOMPtr<nsILocalFile> oldPathFile;
  nsCOMPtr<nsIAtom> folderRenameAtom;
  nsresult rv = GetFilePath(getter_AddRefs(oldPathFile));
  if (NS_FAILED(rv))
    return rv;
  nsCOMPtr<nsIMsgFolder> parentFolder;
  rv = GetParent(getter_AddRefs(parentFolder));
  if (!parentFolder)
    return NS_ERROR_FAILURE;
  nsCOMPtr<nsISupports> parentSupport = do_QueryInterface(parentFolder);
  nsCOMPtr<nsILocalFile> oldSummaryFile;
  rv = GetSummaryFileLocation(oldPathFile, getter_AddRefs(oldSummaryFile));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> dirFile;
  PRInt32 count = mSubFolders.Count();

  if (count > 0)
    rv = CreateDirectoryForFolder(getter_AddRefs(dirFile));

  nsAutoString newDiskName(aNewName);
  NS_MsgHashIfNecessary(newDiskName);

  if (mName.Equals(aNewName, nsCaseInsensitiveStringComparator()))
  {
    rv = ThrowAlertMsg("folderExists", msgWindow);
    return NS_MSG_FOLDER_EXISTS;
  }
  else
  {
    nsCOMPtr <nsILocalFile> parentPathFile;
    parentFolder->GetFilePath(getter_AddRefs(parentPathFile));
    NS_ENSURE_SUCCESS(rv,rv);
    PRBool isDirectory = PR_FALSE;
    parentPathFile->IsDirectory(&isDirectory);
    if (!isDirectory)
      AddDirectorySeparator(parentPathFile);

    rv = CheckIfFolderExists(aNewName, parentFolder, msgWindow);
    if (NS_FAILED(rv))
      return rv;
  }

  ForceDBClosed();

  // Save of dir name before appending .msf
  nsAutoString newNameDirStr(newDiskName);

  if (! (mFlags & nsMsgFolderFlags::Virtual))
    rv = oldPathFile->MoveTo(nsnull, newDiskName);
  if (NS_SUCCEEDED(rv))
  {
    newDiskName.AppendLiteral(SUMMARY_SUFFIX);
    oldSummaryFile->MoveTo(nsnull, newDiskName);
  }
  else
  {
    ThrowAlertMsg("folderRenameFailed", msgWindow);
    return rv;
  }

  if (NS_SUCCEEDED(rv) && count > 0)
  {
    // rename "*.sbd" directory
    newNameDirStr.AppendLiteral(".sbd");
    dirFile->MoveTo(nsnull, newNameDirStr);
  }

  nsCOMPtr<nsIMsgFolder> newFolder;
  if (parentSupport)
  {
    rv = parentFolder->AddSubfolder(aNewName, getter_AddRefs(newFolder));
    if (newFolder)
    {
      newFolder->SetPrettyName(aNewName);
      newFolder->SetFlags(mFlags);
      PRBool changed = PR_FALSE;
      MatchOrChangeFilterDestination(newFolder, PR_TRUE /*caseInsenstive*/, &changed);
      if (changed)
        AlertFilterChanged(msgWindow);

      if (count > 0)
        newFolder->RenameSubFolders(msgWindow, this);

      if (parentFolder)
      {
        SetParent(nsnull);
        parentFolder->PropagateDelete(this, PR_FALSE, msgWindow);
        parentFolder->NotifyItemAdded(newFolder);
      }
      folderRenameAtom = MsgGetAtom("RenameCompleted");
      newFolder->NotifyFolderEvent(folderRenameAtom);
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::RenameSubFolders(nsIMsgWindow *msgWindow, nsIMsgFolder *oldFolder)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::ContainsChildNamed(const nsAString& name, PRBool* containsChild)
{
  NS_ENSURE_ARG_POINTER(containsChild);
  nsCOMPtr<nsIMsgFolder> child;
  GetChildNamed(name, getter_AddRefs(child));
  *containsChild = child != nsnull;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::IsAncestorOf(nsIMsgFolder *child, PRBool *isAncestor)
{
  NS_ENSURE_ARG_POINTER(isAncestor);
  nsresult rv = NS_OK;

  PRInt32 count = mSubFolders.Count();

  for (PRInt32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgFolder> folder(mSubFolders[i]);
    if (folder.get() == child)
      *isAncestor = PR_TRUE;
    else
      folder->IsAncestorOf(child, isAncestor);

    if (*isAncestor)
      return NS_OK;
  }
  *isAncestor = PR_FALSE;
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GenerateUniqueSubfolderName(const nsAString& prefix,
                                                         nsIMsgFolder *otherFolder,
                                                         nsAString& name)
{
  /* only try 256 times */
  for (int count = 0; count < 256; count++)
  {
    nsAutoString uniqueName;
    uniqueName.Assign(prefix);
    uniqueName.AppendInt(count);
    PRBool containsChild;
    PRBool otherContainsChild = PR_FALSE;
    ContainsChildNamed(uniqueName, &containsChild);
    if (otherFolder)
      otherFolder->ContainsChildNamed(uniqueName, &otherContainsChild);

    if (!containsChild && !otherContainsChild)
    {
      name = uniqueName;
      break;
    }
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::UpdateSummaryTotals(PRBool force)
{
  if (!mNotifyCountChanges)
    return NS_OK;

  PRInt32 oldUnreadMessages = mNumUnreadMessages + mNumPendingUnreadMessages;
  PRInt32 oldTotalMessages = mNumTotalMessages + mNumPendingTotalMessages;
  //We need to read this info from the database
  nsresult rv = ReadDBFolderInfo(force);
  
  if (NS_SUCCEEDED(rv))
  {
    PRInt32 newUnreadMessages = mNumUnreadMessages + mNumPendingUnreadMessages;
    PRInt32 newTotalMessages = mNumTotalMessages + mNumPendingTotalMessages;

    //Need to notify listeners that total count changed.
    if(oldTotalMessages != newTotalMessages)
      NotifyIntPropertyChanged(kTotalMessagesAtom, oldTotalMessages, newTotalMessages);

    if(oldUnreadMessages != newUnreadMessages)
      NotifyIntPropertyChanged(kTotalUnreadMessagesAtom, oldUnreadMessages, newUnreadMessages);

    FlushToFolderCache();
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SummaryChanged()
{
  UpdateSummaryTotals(PR_FALSE);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetNumUnread(PRBool deep, PRInt32 *numUnread)
{
  NS_ENSURE_ARG_POINTER(numUnread);

  PRInt32 total = mNumUnreadMessages + mNumPendingUnreadMessages;
  if (deep)
  {
    if (total < 0) // deep search never returns negative counts
      total = 0;
    PRInt32 count = mSubFolders.Count();
    for (PRInt32 i = 0; i < count; i++)
    {
      nsCOMPtr<nsIMsgFolder> folder(mSubFolders[i]);
      PRInt32 num;
      PRUint32 folderFlags;
      folder->GetFlags(&folderFlags);
      if (!(folderFlags & nsMsgFolderFlags::Virtual))
      {
        folder->GetNumUnread(deep, &num);
        total += num;
      }
    }
  }
  *numUnread = total;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetTotalMessages(PRBool deep, PRInt32 *totalMessages)
{
  NS_ENSURE_ARG_POINTER(totalMessages);

  PRInt32 total = mNumTotalMessages + mNumPendingTotalMessages;
  if (deep)
  {
    if (total < 0) // deep search never returns negative counts
      total = 0;
    PRInt32 count = mSubFolders.Count();
    for (PRInt32 i = 0; i < count; i++)
    {
      nsCOMPtr<nsIMsgFolder> folder(mSubFolders[i]);
      PRInt32 num;
      PRUint32 folderFlags;
      folder->GetFlags(&folderFlags);
      if (!(folderFlags & nsMsgFolderFlags::Virtual))
      {
        folder->GetTotalMessages(deep, &num);
        total += num;
      }
    }
  }
  *totalMessages = total;
  return NS_OK;
}

PRInt32 nsMsgDBFolder::GetNumPendingUnread()
{
  return mNumPendingUnreadMessages;
}

PRInt32 nsMsgDBFolder::GetNumPendingTotalMessages()
{
  return mNumPendingTotalMessages;
}

void nsMsgDBFolder::ChangeNumPendingUnread(PRInt32 delta)
{
  if (delta)
  {
    PRInt32 oldUnreadMessages = mNumUnreadMessages + mNumPendingUnreadMessages;
    mNumPendingUnreadMessages += delta;
    PRInt32 newUnreadMessages = mNumUnreadMessages + mNumPendingUnreadMessages;
    NS_ASSERTION(newUnreadMessages >= 0, "shouldn't have negative unread message count");
    if (newUnreadMessages >= 0)
    {
      nsCOMPtr<nsIMsgDatabase> db;
      nsCOMPtr<nsIDBFolderInfo> folderInfo;
      nsresult rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
      if (NS_SUCCEEDED(rv) && folderInfo)
        folderInfo->SetImapUnreadPendingMessages(mNumPendingUnreadMessages);
      NotifyIntPropertyChanged(kTotalUnreadMessagesAtom, oldUnreadMessages, newUnreadMessages);
    }
  }
}

void nsMsgDBFolder::ChangeNumPendingTotalMessages(PRInt32 delta)
{
  if (delta)
  {
    PRInt32 oldTotalMessages = mNumTotalMessages + mNumPendingTotalMessages;
    mNumPendingTotalMessages += delta;
    PRInt32 newTotalMessages = mNumTotalMessages + mNumPendingTotalMessages;

    nsCOMPtr<nsIMsgDatabase> db;
    nsCOMPtr<nsIDBFolderInfo> folderInfo;
    nsresult rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
    if (NS_SUCCEEDED(rv) && folderInfo)
      folderInfo->SetImapTotalPendingMessages(mNumPendingTotalMessages);
    NotifyIntPropertyChanged(kTotalMessagesAtom, oldTotalMessages, newTotalMessages);
  }
}

NS_IMETHODIMP nsMsgDBFolder::SetFlag(PRUint32 flag)
{
  // If calling this function causes us to open the db (i.e., it was not
  // open before), we're going to close the db before returning.
  PRBool dbWasOpen = mDatabase != nsnull;

  ReadDBFolderInfo(PR_FALSE);
  // OnFlagChange can be expensive, so don't call it if we don't need to
  PRBool flagSet;
  nsresult rv;

  if (NS_FAILED(rv = GetFlag(flag, &flagSet)))
    return rv;

  if (!flagSet)
  {
    mFlags |= flag;
    OnFlagChange(flag);
  }
  if (!dbWasOpen && mDatabase)
    SetMsgDatabase(nsnull);

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ClearFlag(PRUint32 flag)
{
  // OnFlagChange can be expensive, so don't call it if we don't need to
  PRBool flagSet;
  nsresult rv;

  if (NS_FAILED(rv = GetFlag(flag, &flagSet)))
    return rv;

  if (flagSet)
  {
    mFlags &= ~flag;
    OnFlagChange (flag);
  }

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetFlag(PRUint32 flag, PRBool *_retval)
{
  *_retval = ((mFlags & flag) != 0);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ToggleFlag(PRUint32 flag)
{
  mFlags ^= flag;
  OnFlagChange (flag);

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::OnFlagChange(PRUint32 flag)
{
  nsresult rv = NS_OK;
  nsCOMPtr<nsIMsgDatabase> db;
  nsCOMPtr<nsIDBFolderInfo> folderInfo;
  rv = GetDBFolderInfoAndDB(getter_AddRefs(folderInfo), getter_AddRefs(db));
  if (NS_SUCCEEDED(rv) && folderInfo)
  {
#ifdef DEBUG_bienvenu1
     nsString name;
     rv = GetName(name);
     NS_ASSERTION(Compare(name, kLocalizedTrashName) || (mFlags & nsMsgFolderFlags::Trash), "lost trash flag");
#endif
    folderInfo->SetFlags((PRInt32) mFlags);
    if (db)
      db->Commit(nsMsgDBCommitType::kLargeCommit);

    if (mFlags & flag)
      NotifyIntPropertyChanged(mFolderFlagAtom, mFlags & ~flag, mFlags);
    else
      NotifyIntPropertyChanged(mFolderFlagAtom, mFlags | flag, mFlags);

    if (flag & nsMsgFolderFlags::Offline)
    {
      PRBool newValue = mFlags & nsMsgFolderFlags::Offline;
      rv = NotifyBoolPropertyChanged(kSynchronizeAtom, !newValue, newValue);
    }
    else if (flag & nsMsgFolderFlags::Elided)
    {
      PRBool newValue = mFlags & nsMsgFolderFlags::Elided;
      rv = NotifyBoolPropertyChanged(kOpenAtom, newValue, !newValue);
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::SetFlags(PRUint32 aFlags)
{
  if (mFlags != aFlags)
  {
    PRUint32 changedFlags = aFlags ^ mFlags;
    mFlags = aFlags;
    OnFlagChange(changedFlags);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetFolderWithFlags(PRUint32 aFlags, nsIMsgFolder** aResult)
{
  if ((mFlags & aFlags) == aFlags)
  {
    NS_ADDREF(*aResult = this);
    return NS_OK;
  }

  GetSubFolders(nsnull); // initialize mSubFolders

  PRInt32 count = mSubFolders.Count();
  *aResult = nsnull;
  for (PRInt32 i = 0; !*aResult && i < count; ++i)
    mSubFolders[i]->GetFolderWithFlags(aFlags, aResult);

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetFoldersWithFlags(PRUint32 aFlags, nsIArray** aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);

  nsresult rv;
  nsCOMPtr<nsIMutableArray> array(do_CreateInstance(NS_ARRAY_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  ListFoldersWithFlags(aFlags, array);
  NS_ADDREF(*aResult = array);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ListFoldersWithFlags(PRUint32 aFlags, nsIMutableArray* aFolders)
{
  NS_ENSURE_ARG_POINTER(aFolders);
  if ((mFlags & aFlags) == aFlags)
    aFolders->AppendElement(static_cast<nsRDFResource*>(this), PR_FALSE);

  GetSubFolders(nsnull); // initialize mSubFolders

  PRInt32 count = mSubFolders.Count();
  for (PRInt32 i = 0; i < count; ++i)
    mSubFolders[i]->ListFoldersWithFlags(aFlags, aFolders);

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::IsSpecialFolder(PRUint32 aFlags,
                                             PRBool aCheckAncestors,
                                             PRBool *aIsSpecial)
{
  NS_ENSURE_ARG_POINTER(aIsSpecial);

  if ((mFlags & aFlags) == 0)
  {
    nsCOMPtr<nsIMsgFolder> parentMsgFolder;
    GetParent(getter_AddRefs(parentMsgFolder));

    if (parentMsgFolder && aCheckAncestors)
      parentMsgFolder->IsSpecialFolder(aFlags, aCheckAncestors, aIsSpecial);
    else
      *aIsSpecial = PR_FALSE;
  }
  else
  {
    // The user can set their INBOX to be their SENT folder.
    // in that case, we want this folder to act like an INBOX,
    // and not a SENT folder
    *aIsSpecial = !((aFlags & nsMsgFolderFlags::SentMail) &&
                    (mFlags & nsMsgFolderFlags::Inbox));
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetExpansionArray(nsISupportsArray *expansionArray)
{
  NS_ENSURE_ARG_POINTER(expansionArray);
  // the application of flags in GetExpansionArray is subtly different
  // than in GetFoldersWithFlags

  nsresult rv;
  PRInt32 count = mSubFolders.Count();

  for (PRInt32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgFolder> folder(mSubFolders[i]);
    PRUint32 cnt2;
    rv = expansionArray->Count(&cnt2);
    if (NS_SUCCEEDED(rv))
    {
      expansionArray->InsertElementAt(folder, cnt2);
      PRUint32 flags;
      folder->GetFlags(&flags);
      if (!(flags & nsMsgFolderFlags::Elided))
        folder->GetExpansionArray(expansionArray);
    }
  }

  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::GetDeletable(PRBool *deletable)
{
  NS_ENSURE_ARG_POINTER(deletable);
  *deletable = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetRequiresCleanup(PRBool *requiredCleanup)
{
  NS_ENSURE_ARG_POINTER(requiredCleanup);
  *requiredCleanup = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::ClearRequiresCleanup()
{
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetKnowsSearchNntpExtension(PRBool *knowsExtension)
{
  NS_ENSURE_ARG_POINTER(knowsExtension);
  *knowsExtension = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetAllowsPosting(PRBool *allowsPosting)
{
  NS_ENSURE_ARG_POINTER(allowsPosting);
  *allowsPosting = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetDisplayRecipients(PRBool *displayRecipients)
{
  nsresult rv;
  *displayRecipients = PR_FALSE;
  if (mFlags & nsMsgFolderFlags::SentMail && !(mFlags & nsMsgFolderFlags::Inbox))
    *displayRecipients = PR_TRUE;
  else if (mFlags & nsMsgFolderFlags::Queue)
    *displayRecipients = PR_TRUE;
  else
  {
    // Only mail folders can be FCC folders
    if (mFlags & nsMsgFolderFlags::Mail || mFlags & nsMsgFolderFlags::ImapBox)
    {
      // There's one FCC folder for sent mail, and one for sent news
      nsIMsgFolder *fccFolders[2];
      int numFccFolders = 0;
      for (int i = 0; i < numFccFolders; i++)
      {
        PRBool isAncestor;
        if (NS_SUCCEEDED(rv = fccFolders[i]->IsAncestorOf(this, &isAncestor)))
        {
          if (isAncestor)
            *displayRecipients = PR_TRUE;
        }
        NS_RELEASE(fccFolders[i]);
      }
    }
  }
  return NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::AcquireSemaphore(nsISupports *semHolder)
{
  nsresult rv = NS_OK;
  if (mSemaphoreHolder == NULL)
    mSemaphoreHolder = semHolder; //Don't AddRef due to ownership issues.
  else
    rv = NS_MSG_FOLDER_BUSY;
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::ReleaseSemaphore(nsISupports *semHolder)
{
  if (!mSemaphoreHolder || mSemaphoreHolder == semHolder)
    mSemaphoreHolder = NULL;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::TestSemaphore(nsISupports *semHolder, PRBool *result)
{
  NS_ENSURE_ARG_POINTER(result);
  *result = (mSemaphoreHolder == semHolder);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetLocked(PRBool *isLocked)
{
  *isLocked =  mSemaphoreHolder != NULL;
  return  NS_OK;
}


NS_IMETHODIMP nsMsgDBFolder::GetRelativePathName(nsACString& pathName)
{
  pathName.Truncate();
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetSizeOnDisk(PRUint32 *size)
{
  NS_ENSURE_ARG_POINTER(size);
  *size = 0;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetSizeOnDisk(PRUint32 aSizeOnDisk)
{
  NotifyIntPropertyChanged(kFolderSizeAtom, mFolderSize, aSizeOnDisk);
  mFolderSize = aSizeOnDisk;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetUsername(nsACString& userName)
{
  nsresult rv;
  nsCOMPtr <nsIMsgIncomingServer> server;
  rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetUsername(userName);
}

NS_IMETHODIMP nsMsgDBFolder::GetHostname(nsACString& hostName)
{
  nsresult rv;
  nsCOMPtr<nsIMsgIncomingServer> server;
  rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetHostName(hostName);
}

NS_IMETHODIMP nsMsgDBFolder::GetNewMessages(nsIMsgWindow *, nsIUrlListener * /* aListener */)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::GetBiffState(PRUint32 *aBiffState)
{
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetBiffState(aBiffState);
}

NS_IMETHODIMP nsMsgDBFolder::SetBiffState(PRUint32 aBiffState)
{
  PRUint32 oldBiffState;
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  if (NS_SUCCEEDED(rv) && server)
    rv = server->GetBiffState(&oldBiffState);

  if (oldBiffState != aBiffState)
  {
    // Get the server and notify it and not inbox.
    if (!mIsServer)
    {
      nsCOMPtr<nsIMsgFolder> folder;
      rv = GetRootFolder(getter_AddRefs(folder));
      if (NS_SUCCEEDED(rv) && folder)
        return folder->SetBiffState(aBiffState);
    }
    if (server)
      server->SetBiffState(aBiffState);

    NotifyIntPropertyChanged(kBiffStateAtom, oldBiffState, aBiffState);
  }
  else if (aBiffState == oldBiffState && aBiffState == nsMsgBiffState_NewMail)
  {
    // biff is already set, but notify that there is additional new mail for the folder
    NotifyIntPropertyChanged(kNewMailReceivedAtom, 0, mNumNewBiffMessages);
  }
  else if (aBiffState == nsMsgBiffState_NoMail)
  {
    // even if the old biff state equals the new biff state, it is still possible that we've never
    // cleared the number of new messages for this particular folder. This happens when the new mail state
    // got cleared by viewing a new message in folder that is different from this one. Biff state is stored per server
    //  the num. of new messages is per folder.
    SetNumNewMessages(0);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetNumNewMessages(PRBool deep, PRInt32 *aNumNewMessages)
{
  NS_ENSURE_ARG_POINTER(aNumNewMessages);

  PRInt32 numNewMessages = (!deep || ! (mFlags & nsMsgFolderFlags::Virtual))
    ? mNumNewBiffMessages : 0;
  if (deep)
  { 
    PRInt32 count = mSubFolders.Count();
    for (PRInt32 i = 0; i < count; i++)
    {
      PRInt32 num;
      mSubFolders[i]->GetNumNewMessages(deep, &num);
      if (num > 0) // it's legal for counts to be negative if we don't know
        numNewMessages += num;
    }
  }
  *aNumNewMessages = numNewMessages;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetNumNewMessages(PRInt32 aNumNewMessages)
{
  if (aNumNewMessages != mNumNewBiffMessages)
  {
    PRInt32 oldNumMessages = mNumNewBiffMessages;
    mNumNewBiffMessages = aNumNewMessages;

    nsCAutoString oldNumMessagesStr;
    oldNumMessagesStr.AppendInt(oldNumMessages);
    nsCAutoString newNumMessagesStr;
    newNumMessagesStr.AppendInt(aNumNewMessages);
    NotifyPropertyChanged(kNumNewBiffMessagesAtom, oldNumMessagesStr, newNumMessagesStr);
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetRootFolder(nsIMsgFolder * *aRootFolder)
{
  NS_ENSURE_ARG_POINTER(aRootFolder);
  nsresult rv;
  nsCOMPtr<nsIMsgIncomingServer> server;
  rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetRootMsgFolder(aRootFolder);
}

NS_IMETHODIMP
nsMsgDBFolder::SetFilePath(nsILocalFile *aFile)
{
  mPath = aFile;
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetFilePath(nsILocalFile * *aFile)
{
  NS_ENSURE_ARG_POINTER(aFile);
  nsresult rv;
  // make a new nsILocalFile object in case the caller
  // alters the underlying file object.
  nsCOMPtr <nsILocalFile> file = do_CreateInstance(NS_LOCAL_FILE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!mPath)
    parseURI(PR_TRUE);
  rv = file->InitWithFile(mPath);
  file.swap(*aFile);
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::MarkMessagesRead(nsIArray *messages, PRBool markRead)
{
  PRUint32 count;
  nsresult rv;

  rv = messages->GetLength(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  for(PRUint32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(messages, i, &rv);
    if (message)
      rv = message->MarkRead(markRead);
    if (NS_FAILED(rv))
      return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::MarkMessagesFlagged(nsIArray *messages, PRBool markFlagged)
{
  PRUint32 count;
  nsresult rv;

  rv = messages->GetLength(&count);
  NS_ENSURE_SUCCESS(rv, rv);

  for(PRUint32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(messages, i, &rv);
    if (message)
      rv = message->MarkFlagged(markFlagged);
    if (NS_FAILED(rv))
      return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetLabelForMessages(nsIArray *aMessages, nsMsgLabelValue aLabel)
{
  NS_ENSURE_ARG(aMessages);
  GetDatabase();
  if (mDatabase)
  {
    PRUint32 count;
    nsresult rv = aMessages->GetLength(&count);
    NS_ENSURE_SUCCESS(rv, rv);
    for(PRUint32 i = 0; i < count; i++)
    {
      nsMsgKey msgKey;
      nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(aMessages, i, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      (void) message->GetMessageKey(&msgKey);
      rv = mDatabase->SetLabel(msgKey, aLabel);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::SetJunkScoreForMessages(nsIArray *aMessages, const nsACString& junkScore)
{
  NS_ENSURE_ARG(aMessages);
  nsresult rv = NS_OK;
  GetDatabase();
  if (mDatabase)
  {
    PRUint32 count;
    nsresult rv = aMessages->GetLength(&count);
    NS_ENSURE_SUCCESS(rv, rv);

    for(PRUint32 i = 0; i < count; i++)
    {
      nsMsgKey msgKey;
      nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(aMessages, i, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      (void) message->GetMessageKey(&msgKey);
      mDatabase->SetStringProperty(msgKey, "junkscore", nsCString(junkScore).get());
      mDatabase->SetStringProperty(msgKey, "junkscoreorigin", "filter");
    }
  }
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::ApplyRetentionSettings()
{
  return ApplyRetentionSettings(PR_TRUE);
}

nsresult nsMsgDBFolder::ApplyRetentionSettings(PRBool deleteViaFolder)
{
  if (mFlags & nsMsgFolderFlags::Virtual) // ignore virtual folders.
    return NS_OK;
  nsresult rv;
  PRBool weOpenedDB = PR_FALSE;
  if (!mDatabase)
  {
    rv = GetDatabase();
    NS_ENSURE_SUCCESS(rv, rv);
    weOpenedDB = PR_TRUE;
  }
  if (mDatabase)
  {
    nsCOMPtr<nsIMsgRetentionSettings> retentionSettings;
    rv = GetRetentionSettings(getter_AddRefs(retentionSettings));
    if (NS_SUCCEEDED(rv))
       rv = mDatabase->ApplyRetentionSettings(retentionSettings, deleteViaFolder);
    // we don't want applying retention settings to keep the db open, because
    // if we try to purge a bunch of folders, that will leave the dbs all open.
    // So if we opened the db, close it.
    if (weOpenedDB)
      CloseDBIfFolderNotOpen();
  }
  return rv;
}

NS_IMETHODIMP
nsMsgDBFolder::DeleteMessages(nsIArray *messages,
                              nsIMsgWindow *msgWindow,
                              PRBool deleteStorage,
                              PRBool isMove,
                              nsIMsgCopyServiceListener *listener,
                              PRBool allowUndo)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDBFolder::CopyMessages(nsIMsgFolder* srcFolder,
                          nsIArray *messages,
                          PRBool isMove,
                          nsIMsgWindow *window,
                          nsIMsgCopyServiceListener* listener,
                          PRBool isFolder,
                          PRBool allowUndo)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDBFolder::CopyFolder(nsIMsgFolder* srcFolder,
                        PRBool isMoveFolder,
                        nsIMsgWindow *window,
                        nsIMsgCopyServiceListener* listener)
{
  NS_ASSERTION(PR_FALSE, "should be overridden by child class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMsgDBFolder::CopyFileMessage(nsIFile* aFile,
                             nsIMsgDBHdr* messageToReplace,
                             PRBool isDraftOrTemplate,
                             PRUint32 aNewMsgFlags,
                             const nsACString &aNewMsgKeywords,
                             nsIMsgWindow *window,
                             nsIMsgCopyServiceListener* listener)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::CopyDataToOutputStreamForAppend(nsIInputStream *aInStream,
                     PRInt32 aLength, nsIOutputStream *aOutputStream)
{
  if (!aInStream)
    return NS_OK;

  PRUint32 uiWritten;
  return aOutputStream->WriteFrom(aInStream, aLength, &uiWritten);
}

NS_IMETHODIMP nsMsgDBFolder::CopyDataDone()
{
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::NotifyPropertyChanged(nsIAtom *aProperty,
                                     const nsACString& aOldValue,
                                     const nsACString& aNewValue)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemPropertyChanged,
                                     (this, aProperty,
                                      nsCString(aOldValue).get(),
                                      nsCString(aNewValue).get()));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemPropertyChanged(this, aProperty,
                                                      nsCString(aOldValue).get(),
                                                      nsCString(aNewValue).get());
}

NS_IMETHODIMP
nsMsgDBFolder::NotifyUnicharPropertyChanged(nsIAtom *aProperty,
                                          const nsAString& aOldValue,
                                          const nsAString& aNewValue)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemUnicharPropertyChanged,
                                     (this, aProperty,
                                      nsString(aOldValue).get(),
                                      nsString(aNewValue).get()));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemUnicharPropertyChanged(this,
                                                 aProperty,
                                                 nsString(aOldValue).get(),
                                                 nsString(aNewValue).get());
}

NS_IMETHODIMP
nsMsgDBFolder::NotifyIntPropertyChanged(nsIAtom *aProperty, PRInt32 aOldValue,
                                        PRInt32 aNewValue)
{
  // Don't send off count notifications if they are turned off.
  if (!mNotifyCountChanges &&
      ((aProperty == kTotalMessagesAtom) ||
       (aProperty == kTotalUnreadMessagesAtom)))
    return NS_OK;

  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemIntPropertyChanged,
                                     (this, aProperty, aOldValue, aNewValue));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemIntPropertyChanged(this, aProperty,
                                                         aOldValue, aNewValue);
}

NS_IMETHODIMP
nsMsgDBFolder::NotifyBoolPropertyChanged(nsIAtom* aProperty,
                                         PRBool aOldValue, PRBool aNewValue)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemBoolPropertyChanged,
                                     (this, aProperty, aOldValue, aNewValue));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemBoolPropertyChanged(this, aProperty,
                                                          aOldValue, aNewValue);
}

NS_IMETHODIMP
nsMsgDBFolder::NotifyPropertyFlagChanged(nsIMsgDBHdr *aItem, nsIAtom *aProperty,
                                         PRUint32 aOldValue, PRUint32 aNewValue)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemPropertyFlagChanged,
                                     (aItem, aProperty, aOldValue, aNewValue));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemPropertyFlagChanged(aItem, aProperty,
                                                          aOldValue, aNewValue);
}

NS_IMETHODIMP nsMsgDBFolder::NotifyItemAdded(nsISupports *aItem)
{
  static PRBool notify = PR_TRUE;

  if (!notify)
    return NS_OK;

  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemAdded,
                                     (this, aItem));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemAdded(this, aItem);
}

nsresult nsMsgDBFolder::NotifyItemRemoved(nsISupports *aItem)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemRemoved,
                                     (this, aItem));

  // Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemRemoved(this, aItem);
}

nsresult nsMsgDBFolder::NotifyFolderEvent(nsIAtom* aEvent)
{
  NS_OBSERVER_ARRAY_NOTIFY_OBSERVERS(mListeners, nsIFolderListener,
                                     OnItemEvent,
                                     (this, aEvent));

  //Notify listeners who listen to every folder
  nsresult rv;
  nsCOMPtr<nsIFolderListener> folderListenerManager =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return folderListenerManager->OnItemEvent(this, aEvent);
}

NS_IMETHODIMP
nsMsgDBFolder::GetFilterList(nsIMsgWindow *aMsgWindow, nsIMsgFilterList **aResult)
{
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetFilterList(aMsgWindow, aResult);
}

NS_IMETHODIMP
nsMsgDBFolder::SetFilterList(nsIMsgFilterList *aFilterList)
{
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->SetFilterList(aFilterList);
}

NS_IMETHODIMP
nsMsgDBFolder::GetEditableFilterList(nsIMsgWindow *aMsgWindow, nsIMsgFilterList **aResult)
{
  NS_ENSURE_ARG_POINTER(aResult);
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->GetEditableFilterList(aMsgWindow, aResult);
}

NS_IMETHODIMP
nsMsgDBFolder::SetEditableFilterList(nsIMsgFilterList *aFilterList)
{
  nsCOMPtr<nsIMsgIncomingServer> server;
  nsresult rv = GetServer(getter_AddRefs(server));
  NS_ENSURE_SUCCESS(rv, rv);
  return server->SetEditableFilterList(aFilterList);
}

/* void enableNotifications (in long notificationType, in boolean enable); */
NS_IMETHODIMP nsMsgDBFolder::EnableNotifications(PRInt32 notificationType, PRBool enable, PRBool dbBatching)
{
  if (notificationType == nsIMsgFolder::allMessageCountNotifications)
  {
    mNotifyCountChanges = enable;
    // start and stop db batching here. This is under the theory
    // that any time we want to enable and disable notifications,
    // we're probably doing something that should be batched.
    nsCOMPtr <nsIMsgDatabase> database;

    if (dbBatching)  //only if we do dbBatching we need to get db
      GetMsgDatabase(getter_AddRefs(database));

    if (enable)
    {
      if (database)
        database->EndBatch();
      UpdateSummaryTotals(PR_TRUE);
    }
    else if (database)
      return database->StartBatch();
    return NS_OK;
  }
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::GetMessageHeader(nsMsgKey msgKey, nsIMsgDBHdr **aMsgHdr)
{
  NS_ENSURE_ARG_POINTER(aMsgHdr);
  nsCOMPtr <nsIMsgDatabase> database;
  nsresult rv = GetMsgDatabase(getter_AddRefs(database));
  NS_ENSURE_SUCCESS(rv, rv);
  return (database) ? database->GetMsgHdrForKey(msgKey, aMsgHdr) : NS_ERROR_FAILURE;
}

// this gets the deep sub-folders too, e.g., the children of the children
NS_IMETHODIMP nsMsgDBFolder::ListDescendents(nsISupportsArray *descendents)
{
  NS_ENSURE_ARG(descendents);

  PRInt32 count = mSubFolders.Count();
  for (PRInt32 i = 0; i < count; i++)
  {
    nsCOMPtr<nsIMsgFolder> child(mSubFolders[i]);
    descendents->AppendElement(child);
    child->ListDescendents(descendents);  // recurse
  }
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetBaseMessageURI(nsACString& baseMessageURI)
{
  if (mBaseMessageURI.IsEmpty())
    return NS_ERROR_FAILURE;
  baseMessageURI = mBaseMessageURI;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetUriForMsg(nsIMsgDBHdr *msgHdr, nsACString& aURI)
{
  NS_ENSURE_ARG(msgHdr);
  nsMsgKey msgKey;
  msgHdr->GetMessageKey(&msgKey);
  nsCAutoString uri;
  uri.Assign(mBaseMessageURI);

  // append a "#" followed by the message key.
  uri.Append('#');
  uri.AppendInt(msgKey);
  aURI = uri;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GenerateMessageURI(nsMsgKey msgKey, nsACString& aURI)
{
  nsCString uri;
  nsresult rv = GetBaseMessageURI( uri);
  NS_ENSURE_SUCCESS(rv,rv);

  // append a "#" followed by the message key.
  uri.Append('#');
  uri.AppendInt(msgKey);
  aURI = uri;
  return NS_OK;
}

nsresult
nsMsgDBFolder::GetBaseStringBundle(nsIStringBundle **aBundle)
{
  NS_ENSURE_ARG_POINTER(aBundle);
  nsresult rv;
  nsCOMPtr<nsIStringBundleService> bundleService =
         do_GetService(NS_STRINGBUNDLE_CONTRACTID, &rv);
  nsCOMPtr<nsIStringBundle> bundle;
  if (bundleService && NS_SUCCEEDED(rv))
    bundleService->CreateBundle("chrome://messenger/locale/messenger.properties",
                                 getter_AddRefs(bundle));
  bundle.swap(*aBundle);
  return rv;
}

nsresult //Do not use this routine if you have to call it very often because it creates a new bundle each time
nsMsgDBFolder::GetStringFromBundle(const char *msgName, nsString& aResult)
{
  nsresult rv;
  nsCOMPtr <nsIStringBundle> bundle;
  rv = GetBaseStringBundle(getter_AddRefs(bundle));
  if (NS_SUCCEEDED(rv) && bundle)
    rv = bundle->GetStringFromName(NS_ConvertASCIItoUTF16(msgName).get(), getter_Copies(aResult));
  return rv;
}

nsresult
nsMsgDBFolder::ThrowConfirmationPrompt(nsIMsgWindow *msgWindow, const nsAString& confirmString, PRBool *confirmed)
{
  if (msgWindow)
  {
    nsCOMPtr <nsIDocShell> docShell;
    msgWindow->GetRootDocShell(getter_AddRefs(docShell));
    if (docShell)
    {
      nsCOMPtr<nsIPrompt> dialog(do_GetInterface(docShell));
      if (dialog && !confirmString.IsEmpty())
        dialog->Confirm(nsnull, nsString(confirmString).get(), confirmed);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMsgDBFolder::GetStringWithFolderNameFromBundle(const char * msgName, nsAString& aResult)
{
  nsCOMPtr <nsIStringBundle> bundle;
  nsresult rv = GetBaseStringBundle(getter_AddRefs(bundle));
  if (NS_SUCCEEDED(rv) && bundle)
  {
    nsString folderName;
    GetName(folderName);
    const PRUnichar *formatStrings[] =
    {
      folderName.get(),
      kLocalizedBrandShortName
    };

    nsString resultStr;
    rv = bundle->FormatStringFromName(NS_ConvertASCIItoUTF16(msgName).get(),
                                      formatStrings, 2, getter_Copies(resultStr));
    if (NS_SUCCEEDED(rv))
      aResult.Assign(resultStr);
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::ConfirmFolderDeletionForFilter(nsIMsgWindow *msgWindow, PRBool *confirmed)
{
  nsString confirmString;
  nsresult rv = GetStringWithFolderNameFromBundle("confirmFolderDeletionForFilter", confirmString);
  NS_ENSURE_SUCCESS(rv, rv);
  return ThrowConfirmationPrompt(msgWindow, confirmString, confirmed);
}

NS_IMETHODIMP nsMsgDBFolder::ThrowAlertMsg(const char * msgName, nsIMsgWindow *msgWindow)
{
  nsString alertString;
  nsresult rv = GetStringWithFolderNameFromBundle(msgName, alertString);
  if (NS_SUCCEEDED(rv) && !alertString.IsEmpty() && msgWindow)
  {
    nsCOMPtr <nsIDocShell> docShell;
    msgWindow->GetRootDocShell(getter_AddRefs(docShell));
    if (docShell)
    {
      nsCOMPtr<nsIPrompt> dialog(do_GetInterface(docShell));
      if (dialog && !alertString.IsEmpty())
        dialog->Alert(nsnull, alertString.get());
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::AlertFilterChanged(nsIMsgWindow *msgWindow)
{
  NS_ENSURE_ARG(msgWindow);
  nsresult rv = NS_OK;
  PRBool checkBox=PR_FALSE;
  GetWarnFilterChanged(&checkBox);
  if (!checkBox)
  {
    nsCOMPtr <nsIDocShell> docShell;
    msgWindow->GetRootDocShell(getter_AddRefs(docShell));
    nsString alertString;
    rv = GetStringFromBundle("alertFilterChanged", alertString);
    nsString alertCheckbox;
    rv = GetStringFromBundle("alertFilterCheckbox", alertCheckbox);
    if (!alertString.IsEmpty() && !alertCheckbox.IsEmpty() && docShell)
    {
      nsCOMPtr<nsIPrompt> dialog(do_GetInterface(docShell));
      if (dialog)
      {
        dialog->AlertCheck(nsnull, alertString.get(), alertCheckbox.get(), &checkBox);
        SetWarnFilterChanged(checkBox);
      }
    }
  }
  return rv;
}

nsresult
nsMsgDBFolder::GetWarnFilterChanged(PRBool *aVal)
{
  NS_ENSURE_ARG(aVal);
  nsresult rv;
  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = prefBranch->GetBoolPref(PREF_MAIL_WARN_FILTER_CHANGED, aVal);
  if (NS_FAILED(rv))
    *aVal = PR_FALSE;
  return NS_OK;
}

nsresult
nsMsgDBFolder::SetWarnFilterChanged(PRBool aVal)
{
  nsresult rv=NS_OK;
  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  return prefBranch->SetBoolPref(PREF_MAIL_WARN_FILTER_CHANGED, aVal);
}

NS_IMETHODIMP nsMsgDBFolder::NotifyCompactCompleted()
{
  NS_ASSERTION(PR_FALSE, "should be overridden by child class");
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult nsMsgDBFolder::CloseDBIfFolderNotOpen()
{
  nsresult rv;
  nsCOMPtr<nsIMsgMailSession> session =
           do_GetService(NS_MSGMAILSESSION_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  PRBool folderOpen;
  session->IsFolderOpenInWindow(this, &folderOpen);
  if (!folderOpen && ! (mFlags & (nsMsgFolderFlags::Trash | nsMsgFolderFlags::Inbox)))
    SetMsgDatabase(nsnull);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetSortOrder(PRInt32 order)
{
  NS_ASSERTION(PR_FALSE, "not implemented");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::GetSortOrder(PRInt32 *order)
{
  NS_ENSURE_ARG_POINTER(order);

  PRUint32 flags;
  nsresult rv = GetFlags(&flags);
  NS_ENSURE_SUCCESS(rv,rv);

  if (flags & nsMsgFolderFlags::Inbox)
    *order = 0;
  else if (flags & nsMsgFolderFlags::Drafts)
    *order = 1;
  else if (flags & nsMsgFolderFlags::Templates)
    *order = 2;
  else if (flags & nsMsgFolderFlags::SentMail)
    *order = 3;
  else if (flags & nsMsgFolderFlags::Archive)
    *order = 4;
  else if (flags & nsMsgFolderFlags::Junk)
    *order = 5;
  else if (flags & nsMsgFolderFlags::Trash)
    *order = 6;
  else if (flags & nsMsgFolderFlags::Virtual)
    *order = 7;
  else if (flags & nsMsgFolderFlags::Queue)
    *order = 8;
  else
    *order = 9;

  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetSortKey(PRUint32 *aLength, PRUint8 **aKey)
{
  NS_ENSURE_ARG(aKey);
  PRInt32 order;
  nsresult rv = GetSortOrder(&order);
  NS_ENSURE_SUCCESS(rv,rv);
  nsAutoString orderString;
  orderString.AppendInt(order);
  nsString folderName;
  rv = GetName(folderName);
  NS_ENSURE_SUCCESS(rv,rv);
  orderString.Append(folderName);
  return CreateCollationKey(orderString, aKey, aLength);
}

nsresult
nsMsgDBFolder::CreateCollationKey(const nsString &aSource,  PRUint8 **aKey, PRUint32 *aLength)
{
  NS_ENSURE_TRUE(gCollationKeyGenerator, NS_ERROR_NULL_POINTER);
  return gCollationKeyGenerator->AllocateRawSortKey(nsICollation::kCollationCaseInSensitive, aSource, 
                                                    aKey, aLength);
}

NS_IMETHODIMP nsMsgDBFolder::CompareSortKeys(nsIMsgFolder *aFolder, PRInt32 *sortOrder)
{
  PRUint8 *sortKey1=nsnull;
  PRUint8 *sortKey2=nsnull;
  PRUint32 sortKey1Length;
  PRUint32 sortKey2Length;
  nsresult rv = GetSortKey(&sortKey1Length, &sortKey1);
  NS_ENSURE_SUCCESS(rv,rv);
  aFolder->GetSortKey(&sortKey2Length, &sortKey2);
  NS_ENSURE_SUCCESS(rv,rv);

  rv = gCollationKeyGenerator->CompareRawSortKey(sortKey1, sortKey1Length, sortKey2, sortKey2Length, sortOrder);
  PR_Free(sortKey1);
  PR_Free(sortKey2);
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetInVFEditSearchScope (PRBool *aInVFEditSearchScope)
{
  *aInVFEditSearchScope = mInVFEditSearchScope;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::SetInVFEditSearchScope (PRBool aInVFEditSearchScope, PRBool aSetOnSubFolders)
{
  PRBool oldInVFEditSearchScope = mInVFEditSearchScope;
  mInVFEditSearchScope = aInVFEditSearchScope;
  NotifyBoolPropertyChanged(kInVFEditSearchScopeAtom, oldInVFEditSearchScope, mInVFEditSearchScope);
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::FetchMsgPreviewText(nsMsgKey *aKeysToFetch, PRUint32 aNumKeys,
                                                 PRBool aLocalOnly, nsIUrlListener *aUrlListener,
                                                 PRBool *aAsyncResults)
{
  NS_ENSURE_ARG_POINTER(aKeysToFetch);
  NS_ENSURE_ARG_POINTER(aAsyncResults);
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsMsgDBFolder::GetMsgTextFromStream(nsIInputStream *stream, const nsACString &aCharset,
                                                  PRUint32 bytesToRead, PRUint32 aMaxOutputLen,
                                                  PRBool aCompressQuotes, PRBool aStripHTMLTags,
                                                  nsACString &aContentType, nsACString &aMsgText)
{
  /*
   1. non mime message - the message body starts after the blank line following the headers.
   2. mime message, multipart/alternative - we could simply scan for the boundary line,
   advance past its headers, and treat the next few lines as the text.
   3. mime message, text/plain - body follows headers
   4. multipart/mixed - scan past boundary, treat next part as body.
   */

  nsLineBuffer<char> *lineBuffer;
  nsresult rv = NS_InitLineBuffer(&lineBuffer);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString msgText;
  nsAutoString contentType;
  nsAutoString encoding;
  nsCAutoString curLine;
  nsCAutoString charset(aCharset);

  // might want to use a state var instead of bools.
  PRBool msgBodyIsHtml = PR_FALSE;
  PRBool more = PR_TRUE;
  PRBool reachedEndBody = PR_FALSE;
  PRBool isBase64 = PR_FALSE;
  PRBool inMsgBody = PR_FALSE;
  PRBool justPassedEndBoundary = PR_FALSE;

  PRUint32 bytesRead = 0;

  // Both are used to extract data from the headers
  nsCOMPtr<nsIMimeHeaders> mimeHeaders(do_CreateInstance(NS_IMIMEHEADERS_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIMIMEHeaderParam> mimeHdrParam(do_GetService(NS_MIMEHEADERPARAM_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Stack of boundaries, used to figure out where we are
  nsTArray<nsCString> boundaryStack;

  while (!inMsgBody && bytesRead <= bytesToRead)
  {
    nsCAutoString msgHeaders;
    // We want to NS_ReadLine until we get to a blank line (the end of the headers)
    while (more)
    {
      rv = NS_ReadLine(stream, lineBuffer, curLine, &more);
      NS_ENSURE_SUCCESS(rv, rv);
      if (curLine.IsEmpty())
        break;
      msgHeaders.Append(curLine);
      msgHeaders.Append(NS_LITERAL_CSTRING("\r\n"));
      bytesRead += curLine.Length();
      if (bytesRead > bytesToRead)
        break;
    }

    // There's no point in processing if we can't get the body
    if (bytesRead > bytesToRead)
      break;

    // Process the headers, looking for things we need
    rv = mimeHeaders->Initialize(msgHeaders.get(), msgHeaders.Length());
    NS_ENSURE_SUCCESS(rv, rv);

    nsCAutoString contentTypeHdr;
    mimeHeaders->ExtractHeader("Content-Type", PR_FALSE, getter_Copies(contentTypeHdr));

    // Get the content type
    // If we don't have a content type, then we assign text/plain
    // this is in violation of the RFC for multipart/digest, though
    // Also, if we've just passed an end boundary, we're going to ignore this.
    if (!justPassedEndBoundary && contentTypeHdr.IsEmpty())
      contentType.Assign(NS_LITERAL_STRING("text/plain"));
    else
      mimeHdrParam->GetParameter(contentTypeHdr, nsnull, EmptyCString(), PR_FALSE, nsnull, contentType);

    justPassedEndBoundary = PR_FALSE;

    // If we are multipart, then we need to get the boundary
    if (StringBeginsWith(contentType, NS_LITERAL_STRING("multipart/"), nsCaseInsensitiveStringComparator()))
    {
      nsAutoString boundaryParam;
      mimeHdrParam->GetParameter(contentTypeHdr, "boundary", EmptyCString(), PR_FALSE, nsnull, boundaryParam);
      if (!boundaryParam.IsEmpty())
      {
        nsCAutoString boundary(NS_LITERAL_CSTRING("--"));
        boundary.Append(NS_ConvertUTF16toUTF8(boundaryParam));
        boundaryStack.AppendElement(boundary);
      }
    }

    // If we are message/rfc822, then there's another header block coming up
    else if (contentType.Equals(NS_LITERAL_STRING("message/rfc822"), nsCaseInsensitiveStringComparator()))
      continue;

    // If we are a text part, then we want it
    else if (StringBeginsWith(contentType, NS_LITERAL_STRING("text/"), nsCaseInsensitiveStringComparator()))
    {
      inMsgBody = PR_TRUE;

      if (contentType.Equals(NS_LITERAL_STRING("text/html"), nsCaseInsensitiveStringComparator()))
        msgBodyIsHtml = PR_TRUE;

      // Also get the charset if required
      if (charset.IsEmpty())
      {
        nsAutoString charsetW;
        mimeHdrParam->GetParameter(contentTypeHdr, "charset", EmptyCString(), PR_FALSE, nsnull, charsetW);
        charset.Assign(NS_ConvertUTF16toUTF8(charsetW));
      }

      // Finally, get the encoding
      nsCAutoString encodingHdr;
      mimeHeaders->ExtractHeader("Content-Transfer-Encoding", PR_FALSE, getter_Copies(encodingHdr));
      if (!encodingHdr.IsEmpty())
        mimeHdrParam->GetParameter(encodingHdr, nsnull, EmptyCString(), PR_FALSE, nsnull, encoding);

      if (encoding.Equals(NS_LITERAL_STRING("base64"), nsCaseInsensitiveStringComparator()))
        isBase64 = PR_TRUE;
    }

    // We need to consume the rest, until the next headers
    PRUint32 count = boundaryStack.Length();
    nsCAutoString boundary;
    nsCAutoString endBoundary;
    if (count)
    {
      boundary.Assign(boundaryStack.ElementAt(count - 1));
      endBoundary.Assign(boundary);
      endBoundary.Append(NS_LITERAL_CSTRING("--"));
    }
    while (more)
    {
      rv = NS_ReadLine(stream, lineBuffer, curLine, &more);
      NS_ENSURE_SUCCESS(rv, rv);

      if (count)
      {
        // If we've reached a MIME final delimiter, pop and break
        if (StringBeginsWith(curLine, endBoundary))
        {
          if (inMsgBody)
            reachedEndBody = PR_TRUE;
          boundaryStack.RemoveElementAt(count - 1);
          justPassedEndBoundary = PR_TRUE;
          break;
        }
        // If we've reached the end of this MIME part, we can break
        if (StringBeginsWith(curLine, boundary))
        {
          if (inMsgBody)
            reachedEndBody = PR_TRUE;
          break;
        }
      }

      // Only append the text if we're actually in the message body
      if (inMsgBody)
      {
        msgText.Append(curLine);
        if (!isBase64)
          msgText.Append(NS_LITERAL_CSTRING("\r\n"));
      }

      bytesRead += curLine.Length();
      if (bytesRead > bytesToRead)
        break;
    }
  }
  PR_Free(lineBuffer);

  // if the snippet is encoded, decode it
  if (!encoding.IsEmpty())
    decodeMsgSnippet(NS_ConvertUTF16toUTF8(encoding), !reachedEndBody, msgText);

  // In order to turn our snippet into unicode, we need to convert it from the charset we
  // detected earlier.
  nsString unicodeMsgBodyStr;
  ConvertToUnicode(charset.get(), msgText, unicodeMsgBodyStr);

  // now we've got a msg body. If it's html, convert it to plain text.
  if (msgBodyIsHtml && aStripHTMLTags)
    ConvertMsgSnippetToPlainText(unicodeMsgBodyStr, unicodeMsgBodyStr);

  // We want to remove any whitespace from the beginning and end of the string
  unicodeMsgBodyStr.Trim(" \t\r\n", PR_TRUE, PR_TRUE);

  // step 3, optionally remove quoted text from the snippet
  nsString compressedQuotesMsgStr;
  if (aCompressQuotes)
    compressQuotesInMsgSnippet(unicodeMsgBodyStr, compressedQuotesMsgStr);

  // now convert back to utf-8 which is more convenient for storage
  CopyUTF16toUTF8(aCompressQuotes ? compressedQuotesMsgStr : unicodeMsgBodyStr, aMsgText);

  // finally, truncate the string based on aMaxOutputLen
  if (aMsgText.Length() > aMaxOutputLen) {
    if (NS_IsAscii(aMsgText.BeginReading()))
      aMsgText.SetLength(aMaxOutputLen);
    else
      nsMsgI18NShrinkUTF8Str(nsCString(aMsgText),
                             aMaxOutputLen, aMsgText);
  }

  // Also assign the content type being returned
  aContentType.Assign(NS_ConvertUTF16toUTF8(contentType));
  return rv;
}

/**
 * decodeMsgSnippet - helper function which applies the appropriate transfer decoding
 *                    to the message snippet based on aEncodingType. Currently handles
 *                    base64 and quoted-printable. If aEncodingType refers to an encoding we don't
 *                    handle, the message data is passed back unmodified.
 * @param aEncodingType the encoding type (base64, quoted-printable)
 * @param aIsComplete the snippet is actually the entire message so the decoder
 *                           doesn't have to worry about partial data
 * @param aMsgSnippet in/out argument. The encoded msg snippet and then the decoded snippet
 */
void nsMsgDBFolder::decodeMsgSnippet(const nsACString& aEncodingType, PRBool aIsComplete, nsCString& aMsgSnippet)
{
  if (MsgLowerCaseEqualsLiteral(aEncodingType, "base64"))
  {
    PRInt32 base64Len = aMsgSnippet.Length();
    if (aIsComplete)
      base64Len -= base64Len % 4;
    char *decodedBody = PL_Base64Decode(aMsgSnippet.get(), base64Len, nsnull);
    if (decodedBody)
      aMsgSnippet.Adopt(decodedBody);
  }
  else if (MsgLowerCaseEqualsLiteral(aEncodingType, "quoted-printable"))
  {
    // giant hack - decode in place, and truncate string.
    MsgStripQuotedPrintable((unsigned char *) aMsgSnippet.get());
    aMsgSnippet.SetLength(strlen(aMsgSnippet.get()));
  }
}

/**
 * stripQuotesFromMsgSnippet - Reduces quoted reply text including the citation (Scott wrote:) from
 *                             the message snippet to " ... ". Assumes the snippet has been decoded and converted to
 *                             plain text.
 * @param aMsgSnippet in/out argument. The string to strip quotes from.
 */
void nsMsgDBFolder::compressQuotesInMsgSnippet(const nsString& aMsgSnippet, nsAString& aCompressedQuotes)
{
  PRUint32 msgBodyStrLen = aMsgSnippet.Length();
  PRBool lastLineWasAQuote = PR_FALSE;
  PRUint32 offset = 0;
  PRUint32 lineFeedPos = 0;
  while (offset < msgBodyStrLen)
  {
    lineFeedPos = aMsgSnippet.FindChar('\n', offset);
    if (lineFeedPos != -1)
    {
      const nsAString& currentLine = Substring(aMsgSnippet, offset, lineFeedPos - offset);
      // this catches quoted text ("> "), nested quotes of any level (">> ", ">>> ", ...)
      // it also catches empty line quoted text (">"). It might be over agressive and require
      // tweaking later.
      // Try to strip the citation. If the current line ends with a ':' and the next line
      // looks like a quoted reply (starts with a ">") skip the current line
      if (StringBeginsWith(currentLine, NS_LITERAL_STRING(">")) ||
          (lineFeedPos + 1 < msgBodyStrLen  && lineFeedPos
          && aMsgSnippet[lineFeedPos - 1] == PRUnichar(':')
          && aMsgSnippet[lineFeedPos + 1] == PRUnichar('>')))
      {
        lastLineWasAQuote = PR_TRUE;
      }
      else if (!currentLine.IsEmpty())
      {
        if (lastLineWasAQuote)
        {
          aCompressedQuotes += NS_LITERAL_STRING(" ... ");
          lastLineWasAQuote = PR_FALSE;
        }

        aCompressedQuotes += currentLine;
        aCompressedQuotes += PRUnichar(' '); // don't forget to substitute a space for the line feed
      }

      offset = lineFeedPos + 1;
    }
    else
    {
      aCompressedQuotes.Append(Substring(aMsgSnippet, offset, msgBodyStrLen - offset));
      break;
    }
  }
}

NS_IMETHODIMP nsMsgDBFolder::ConvertMsgSnippetToPlainText(
    const nsAString& aMessageText, nsAString& aOutText)
{
  nsString bodyText;
  nsresult rv = NS_OK;

  // Create a parser
  nsCOMPtr<nsIParser> parser = do_CreateInstance(kParserCID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create the appropriate output sink
  nsCOMPtr<nsIContentSink> sink = do_CreateInstance(NS_PLAINTEXTSINK_CONTRACTID,&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHTMLToTextSink> textSink(do_QueryInterface(sink));
  NS_ENSURE_TRUE(textSink, NS_ERROR_FAILURE);
  PRUint32 flags = nsIDocumentEncoder::OutputLFLineBreak
                   | nsIDocumentEncoder::OutputNoScriptContent
                   | nsIDocumentEncoder::OutputNoFramesContent
                   | nsIDocumentEncoder::OutputBodyOnly;

  textSink->Initialize(&bodyText, flags, 80);
  parser->SetContentSink(sink);
  rv = parser->Parse(aMessageText, 0, NS_LITERAL_CSTRING("text/html"), PR_TRUE);
  aOutText.Assign(bodyText);
  return rv;
}

nsresult nsMsgDBFolder::GetMsgPreviewTextFromStream(nsIMsgDBHdr *msgHdr, nsIInputStream *stream)
{
  nsCString msgBody;
  nsCAutoString charset;
  msgHdr->GetCharset(getter_Copies(charset));
  nsCAutoString contentType;
  nsresult rv = GetMsgTextFromStream(stream, charset, 4096, 255, PR_TRUE, PR_TRUE, contentType, msgBody);
  // replaces all tabs and line returns with a space, 
  // then trims off leading and trailing white space
  MsgCompressWhitespace(msgBody);
  msgHdr->SetStringProperty("preview", msgBody.get());
  return rv;
}

void nsMsgDBFolder::SetMRUTime()
{
  PRUint32 seconds;
  PRTime2Seconds(PR_Now(), &seconds);
  nsCAutoString nowStr;
  nowStr.AppendInt(seconds);
  SetStringProperty(MRU_TIME_PROPERTY, nowStr);
}

NS_IMETHODIMP nsMsgDBFolder::AddKeywordsToMessages(nsIArray *aMessages, const nsACString& aKeywords)
{
  NS_ENSURE_ARG(aMessages);
  nsresult rv = NS_OK;
  GetDatabase();
  if (mDatabase)
  {
    PRUint32 count;
    nsresult rv = aMessages->GetLength(&count);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCString keywords;

    for(PRUint32 i = 0; i < count; i++)
    {
      nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(aMessages, i, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      message->GetStringProperty("keywords", getter_Copies(keywords));
      nsTArray<nsCString> keywordArray;
      ParseString(aKeywords, ' ', keywordArray);
      PRUint32 addCount = 0;
      for (PRUint32 j = 0; j < keywordArray.Length(); j++)
      {
        PRInt32 start, length;
        if (!MsgFindKeyword(keywordArray[j], keywords, &start, &length))
        {
          if (!keywords.IsEmpty())
            keywords.Append(' ');
          keywords.Append(keywordArray[j]);
          addCount++;
        }
      }
      // avoid using the message key to set the string property, because
      // in the case of filters running on incoming pop3 mail with quarantining
      // turned on, the message key is wrong.
      mDatabase->SetStringPropertyByHdr(message, "keywords", keywords.get());
      
      if (addCount)
        NotifyPropertyFlagChanged(message, kKeywords, 0, addCount);
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::RemoveKeywordsFromMessages(nsIArray *aMessages, const nsACString& aKeywords)
{
  NS_ENSURE_ARG(aMessages);
  nsresult rv = NS_OK;
  GetDatabase();
  if (mDatabase)
  {
    PRUint32 count;
    nsresult rv = aMessages->GetLength(&count);
    NS_ENSURE_SUCCESS(rv, rv);
    nsTArray<nsCString> keywordArray;
    ParseString(aKeywords, ' ', keywordArray);
    nsCString keywords;
    // If the tag is also a label, we should remove the label too...

    for(PRUint32 i = 0; i < count; i++)
    {
      nsCOMPtr<nsIMsgDBHdr> message = do_QueryElementAt(aMessages, i, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = message->GetStringProperty("keywords", getter_Copies(keywords));
      PRUint32 removeCount = 0;
      for (PRUint32 j = 0; j < keywordArray.Length(); j++)
      {
        PRBool keywordIsLabel = (StringBeginsWith(keywordArray[j], NS_LITERAL_CSTRING("$label"))
          && keywordArray[j].CharAt(6) >= '1' && keywordArray[j].CharAt(6) <= '5');
        if (keywordIsLabel)
        {
          nsMsgLabelValue labelValue;
          message->GetLabel(&labelValue);
          // if we're removing the keyword that corresponds to a pre 2.0 label,
          // we need to clear the old label attribute on the message.
          if (labelValue == (nsMsgLabelValue) (keywordArray[j].CharAt(6) - '0'))
            message->SetLabel((nsMsgLabelValue) 0);
        }
        PRInt32 startOffset, length;
        if (MsgFindKeyword(keywordArray[j], keywords, &startOffset, &length))
        {
          // delete any leading space delimiters
          while (startOffset && keywords.CharAt(startOffset - 1) == ' ')
          {
            startOffset--;
            length++;
          }
          // but if the keyword is at the start then delete the following space
          if (!startOffset && length < keywords.Length() &&
              keywords.CharAt(length) == ' ')
            length++;
          keywords.Cut(startOffset, length);
          removeCount++;
        }
      }

      if (removeCount)
      {
        mDatabase->SetStringPropertyByHdr(message, "keywords", keywords.get());
        NotifyPropertyFlagChanged(message, kKeywords, removeCount, 0);
      }
    }
  }
  return rv;
}

NS_IMETHODIMP nsMsgDBFolder::GetCustomIdentity(nsIMsgIdentity **aIdentity)
{
  NS_ENSURE_ARG_POINTER(aIdentity);
  *aIdentity = nsnull;
  return NS_OK;
}

NS_IMETHODIMP nsMsgDBFolder::GetProcessingFlags(nsMsgKey aKey, PRUint32 *aFlags)
{
  NS_ENSURE_ARG_POINTER(aFlags);
  *aFlags = 0;
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
    if (mProcessingFlag[i].keys && mProcessingFlag[i].keys->IsMember(aKey))
      *aFlags |= mProcessingFlag[i].bit;
  return NS_OK;
}  

NS_IMETHODIMP nsMsgDBFolder::OrProcessingFlags(nsMsgKey aKey, PRUint32 mask)
{
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
    if (mProcessingFlag[i].bit & mask && mProcessingFlag[i].keys)
      mProcessingFlag[i].keys->Add(aKey);
  return NS_OK;
}  

NS_IMETHODIMP nsMsgDBFolder::AndProcessingFlags(nsMsgKey aKey, PRUint32 mask)
{
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
    if (!(mProcessingFlag[i].bit & mask) && mProcessingFlag[i].keys)
      mProcessingFlag[i].keys->Remove(aKey);
  return NS_OK;
}

void nsMsgDBFolder::ClearProcessingFlags()
{
  for (PRUint32 i = 0; i < nsMsgProcessingFlags::NumberOfFlags; i++)
  {
    // There is no clear method so we need to delete and re-create.
    delete mProcessingFlag[i].keys;
    mProcessingFlag[i].keys = nsMsgKeySetU::Create();
  }
}

/* static */ nsMsgKeySetU* nsMsgKeySetU::Create()
{
  nsMsgKeySetU* set = new nsMsgKeySetU;
  if (set)
  {
    set->loKeySet = nsMsgKeySet::Create();
    set->hiKeySet = nsMsgKeySet::Create();
  }
  if (!(set && set->loKeySet && set->hiKeySet))
    delete set;
  return set;
}

nsMsgKeySetU::nsMsgKeySetU()
{ }

nsMsgKeySetU::~nsMsgKeySetU()
{
  delete loKeySet;
  delete hiKeySet;
}

const PRUint32 kLowerBits = 0x7fffffff;

int nsMsgKeySetU::Add(PRUint32 aKey)
{
  PRInt32 intKey = static_cast<PRInt32>(aKey);
  if (intKey >= 0)
    return loKeySet->Add(intKey);
  return hiKeySet->Add(intKey & kLowerBits);
}

int nsMsgKeySetU::Remove(PRUint32 aKey)
{
  PRInt32 intKey = static_cast<PRInt32>(aKey);
  if (intKey >= 0)
    return loKeySet->Remove(intKey);
  return hiKeySet->Remove(intKey & kLowerBits);
}

PRBool nsMsgKeySetU::IsMember(PRUint32 aKey)
{
  PRInt32 intKey = static_cast<PRInt32>(aKey);
  if (intKey >= 0)
    return loKeySet->IsMember(intKey);
  return hiKeySet->IsMember(intKey & kLowerBits);
}

nsresult nsMsgKeySetU::ToMsgKeyArray(nsTArray<nsMsgKey> &aArray)
{
  nsresult rv = loKeySet->ToMsgKeyArray(aArray);
  NS_ENSURE_SUCCESS(rv, rv);
  return hiKeySet->ToMsgKeyArray(aArray);
}


