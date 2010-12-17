/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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

#include "msgCore.h"    // precompiled header...

#include "nsPop3Service.h"
#include "nsIMsgIncomingServer.h"
#include "nsIPop3IncomingServer.h"

#include "nsPop3URL.h"
#include "nsPop3Sink.h"
#include "nsPop3Protocol.h"
#include "nsMsgLocalCID.h"
#include "nsMsgBaseCID.h"
#include "nsCOMPtr.h"
#include "nsIMsgWindow.h"
#include "nsINetUtil.h"

#include "nsIRDFService.h"
#include "nsRDFCID.h"
#include "nsIDirectoryService.h"
#include "nsMailDirServiceDefs.h"
#include "prprf.h"
#include "nsMsgUtils.h"
#include "nsIMsgAccountManager.h"
#include "nsIMsgAccount.h"
#include "nsLocalMailFolder.h"
#include "nsIMailboxUrl.h"
#include "nsInt64.h"
#include "nsIPrompt.h"
#include "nsLocalStrings.h"
#include "nsINetUtil.h"
#include "nsComponentManagerUtils.h"
#include "nsServiceManagerUtils.h"

#define PREF_MAIL_ROOT_POP3 "mail.root.pop3"        // old - for backward compatibility only
#define PREF_MAIL_ROOT_POP3_REL "mail.root.pop3-rel"

static NS_DEFINE_CID(kPop3UrlCID, NS_POP3URL_CID);
static NS_DEFINE_CID(kRDFServiceCID, NS_RDFSERVICE_CID);

nsPop3Service::nsPop3Service()
{
}

nsPop3Service::~nsPop3Service()
{}

NS_IMPL_ISUPPORTS3(nsPop3Service,
                         nsIPop3Service,
                         nsIProtocolHandler,
                         nsIMsgProtocolInfo)

NS_IMETHODIMP nsPop3Service::CheckForNewMail(nsIMsgWindow* aMsgWindow,
                                             nsIUrlListener * aUrlListener,
                                             nsIMsgFolder *aInbox,
                                             nsIPop3IncomingServer *aPopServer,
                                             nsIURI ** aURL)
{
  return GetMail(PR_FALSE /* don't download, just check */, aMsgWindow, aUrlListener, aInbox, aPopServer, aURL);
}


nsresult nsPop3Service::GetNewMail(nsIMsgWindow *aMsgWindow, nsIUrlListener * aUrlListener,
                                   nsIMsgFolder *aInbox,
                                   nsIPop3IncomingServer *aPopServer,
                                   nsIURI ** aURL)
{
  return GetMail(PR_TRUE /* download */, aMsgWindow, aUrlListener, aInbox, aPopServer, aURL);
}

nsresult nsPop3Service::GetMail(PRBool downloadNewMail,
                                nsIMsgWindow* aMsgWindow,
                                nsIUrlListener * aUrlListener,
                                nsIMsgFolder *aInbox,
                                nsIPop3IncomingServer *aPopServer,
                                nsIURI ** aURL)
{

  NS_ENSURE_ARG_POINTER(aInbox);
  PRInt32 popPort = -1;

  nsCOMPtr<nsIMsgIncomingServer> server;
  nsCOMPtr<nsIURI> url;

  server = do_QueryInterface(aPopServer);
  NS_ENSURE_TRUE(server, NS_MSG_INVALID_OR_MISSING_SERVER);

  nsCOMPtr <nsIMsgLocalMailFolder> destLocalFolder = do_QueryInterface(aInbox);
  if (destLocalFolder)
  {
    PRBool destFolderTooBig;
    destLocalFolder->WarnIfLocalFileTooBig(aMsgWindow, &destFolderTooBig);
    if (destFolderTooBig)
      return NS_MSG_ERROR_WRITING_MAIL_FOLDER;
  }

  nsCString popHost;
  nsCString popUser;
  nsresult rv = server->GetHostName(popHost);
  NS_ENSURE_SUCCESS(rv, rv);
  if (popHost.IsEmpty())
    return NS_MSG_INVALID_OR_MISSING_SERVER;

  rv = server->GetPort(&popPort);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = server->GetUsername(popUser);
  NS_ENSURE_SUCCESS(rv, rv);
  if (popUser.IsEmpty())
    return NS_MSG_SERVER_USERNAME_MISSING;

  nsCString escapedUsername;
  MsgEscapeString(popUser, nsINetUtil::ESCAPE_XALPHAS, escapedUsername);

  if (NS_SUCCEEDED(rv) && aPopServer)
  {
    // now construct a pop3 url...
    // we need to escape the username because it may contain
    // characters like / % or @
    char * urlSpec = (downloadNewMail)
      ? PR_smprintf("pop3://%s@%s:%d", escapedUsername.get(), popHost.get(), popPort)
      : PR_smprintf("pop3://%s@%s:%d/?check", escapedUsername.get(), popHost.get(), popPort);
    rv = BuildPop3Url(urlSpec, aInbox, aPopServer, aUrlListener, getter_AddRefs(url), aMsgWindow);
    PR_Free(urlSpec);
  }


  if (NS_SUCCEEDED(rv) && url)
    rv = RunPopUrl(server, url);

  if (aURL && url) // we already have a ref count on pop3url...
    NS_IF_ADDREF(*aURL = url);

  return rv;
}

NS_IMETHODIMP nsPop3Service::VerifyLogon(nsIMsgIncomingServer *aServer,
                                         nsIUrlListener *aUrlListener,
                                         nsIMsgWindow *aMsgWindow,
                                         nsIURI **aURL)
{
  NS_ENSURE_ARG_POINTER(aServer);
  nsCString popHost;
  nsCString popUser;
  PRInt32 popPort = -1;

  nsresult rv = aServer->GetHostName(popHost);
  NS_ENSURE_SUCCESS(rv, rv);
  if (popHost.IsEmpty())
    return NS_MSG_INVALID_OR_MISSING_SERVER;

  rv = aServer->GetPort(&popPort);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aServer->GetUsername(popUser);
  NS_ENSURE_SUCCESS(rv, rv);
  if (popUser.IsEmpty())
    return NS_MSG_SERVER_USERNAME_MISSING;

  nsCString escapedUsername;
  MsgEscapeString(popUser, nsINetUtil::ESCAPE_XALPHAS, escapedUsername);

  nsCOMPtr <nsIPop3IncomingServer> popServer = do_QueryInterface(aServer, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  // now construct a pop3 url...
  // we need to escape the username because it may contain
  // characters like / % or @
  char * urlSpec = PR_smprintf("pop3://%s@%s:%d/?verifyLogon", 
                              escapedUsername.get(), popHost.get(), popPort);
  if (!urlSpec)
    return NS_ERROR_OUT_OF_MEMORY;
  nsCOMPtr<nsIURI> url;
  rv = BuildPop3Url(urlSpec, nsnull, popServer, aUrlListener, 
                    getter_AddRefs(url), aMsgWindow);
  PR_smprintf_free(urlSpec);

  if (NS_SUCCEEDED(rv) && url)
  {
    rv = RunPopUrl(aServer, url);
    if (NS_SUCCEEDED(rv) && aURL)
      url.forget(aURL);
  }

  return rv;
}

nsresult nsPop3Service::BuildPop3Url(const char * urlSpec,
                                     nsIMsgFolder *inbox,
                                     nsIPop3IncomingServer *server,
                                     nsIUrlListener * aUrlListener,
                                     nsIURI ** aUrl,
                                     nsIMsgWindow *aMsgWindow)
{
  nsresult rv;

  nsPop3Sink * pop3Sink = new nsPop3Sink();
  if (pop3Sink)
  {
    pop3Sink->SetPopServer(server);
    pop3Sink->SetFolder(inbox);
  }

  // now create a pop3 url and a protocol instance to run the url....
  nsCOMPtr<nsIPop3URL> pop3Url = do_CreateInstance(kPop3UrlCID, &rv);
  NS_ENSURE_SUCCESS(rv,rv);

  pop3Url->SetPop3Sink(pop3Sink);

  rv = pop3Url->QueryInterface(NS_GET_IID(nsIURI), (void **) aUrl);
  NS_ENSURE_SUCCESS(rv,rv);

  (*aUrl)->SetSpec(nsDependentCString(urlSpec));

  nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(pop3Url);
  if (mailnewsurl)
  {
    if (aUrlListener)
      mailnewsurl->RegisterListener(aUrlListener);
    if (aMsgWindow)
      mailnewsurl->SetMsgWindow(aMsgWindow);
  }

  return rv;
}

nsresult nsPop3Service::RunPopUrl(nsIMsgIncomingServer * aServer, nsIURI * aUrlToRun)
{
  nsresult rv = NS_OK;
  if (aServer && aUrlToRun)
  {
    nsCString userName;

    // load up required server information
    // we store the username unescaped in the server
    // so there is no need to unescape it
    rv = aServer->GetRealUsername(userName);

    // find out if the server is busy or not...if the server is busy, we are
    // *NOT* going to run the url
    PRBool serverBusy = PR_FALSE;
    rv = aServer->GetServerBusy(&serverBusy);

    if (!serverBusy)
    {
      nsPop3Protocol * protocol = new nsPop3Protocol(aUrlToRun);
      if (protocol)
      {
        NS_ADDREF(protocol);
        rv = protocol->Initialize(aUrlToRun);
        if(NS_FAILED(rv))
        {
          NS_RELEASE(protocol);
          return rv;
        }
        // the protocol stores the unescaped username, so there is no need to escape it.
        protocol->SetUsername(userName.get());
        rv = protocol->LoadUrl(aUrlToRun);
        NS_RELEASE(protocol);
        if (NS_FAILED(rv))
          aServer->SetServerBusy(PR_FALSE);
      }
    }
    else
    {
      nsCOMPtr <nsIMsgMailNewsUrl> url = do_QueryInterface(aUrlToRun);
      if (url)
        AlertServerBusy(url);
      rv = NS_ERROR_FAILURE;
    }
  } // if server

  return rv;
}


NS_IMETHODIMP nsPop3Service::GetScheme(nsACString &aScheme)
{
    aScheme = "pop3";
    return NS_OK;
}

NS_IMETHODIMP nsPop3Service::GetDefaultPort(PRInt32 *aDefaultPort)
{
    NS_ENSURE_ARG_POINTER(aDefaultPort);
    *aDefaultPort = nsIPop3URL::DEFAULT_POP3_PORT;
    return NS_OK;
}

NS_IMETHODIMP nsPop3Service::AllowPort(PRInt32 port, const char *scheme, PRBool *_retval)
{
    *_retval = PR_TRUE; // allow pop on any port
    return NS_OK;
}

NS_IMETHODIMP nsPop3Service::GetDefaultDoBiff(PRBool *aDoBiff)
{
    NS_ENSURE_ARG_POINTER(aDoBiff);
    // by default, do biff for POP3 servers
    *aDoBiff = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP nsPop3Service::GetProtocolFlags(PRUint32 *result)
{
    NS_ENSURE_ARG_POINTER(result);
    *result = URI_NORELATIVE | URI_DANGEROUS_TO_LOAD | ALLOWS_PROXY;
    return NS_OK;
}

NS_IMETHODIMP nsPop3Service::NewURI(const nsACString &aSpec,
                                    const char *aOriginCharset, // ignored
                                    nsIURI *aBaseURI,
                                    nsIURI **_retval)
{
    nsresult rv = NS_ERROR_FAILURE;
    if (!_retval) return rv;
    nsCAutoString folderUri(aSpec);
    nsCOMPtr<nsIRDFResource> resource;
    PRInt32 offset = folderUri.Find("?");
    if (offset != -1)
        folderUri.SetLength(offset);

    const char *uidl = PL_strstr(nsCString(aSpec).get(), "uidl=");
    if (!uidl) return NS_ERROR_FAILURE;

    nsCOMPtr<nsIRDFService> rdfService(do_GetService(kRDFServiceCID, &rv));
    if (NS_FAILED(rv)) return rv;
    rv = rdfService->GetResource(folderUri, getter_AddRefs(resource));
    if (NS_FAILED(rv)) return rv;
    nsCOMPtr<nsIMsgFolder> folder = do_QueryInterface(resource, &rv);
    if (NS_FAILED(rv)) return rv;
    nsCOMPtr<nsIMsgIncomingServer> server;

    nsLocalFolderScanState folderScanState;
    nsCOMPtr<nsIMsgLocalMailFolder> localFolder = do_QueryInterface(folder);
    nsCOMPtr <nsIMailboxUrl> mailboxUrl = do_QueryInterface(aBaseURI);

    nsCOMPtr<nsILocalFile> path;
    rv = folder->GetFilePath(getter_AddRefs(path));
    if (NS_FAILED(rv)) return rv;

    folderScanState.m_localFile = path;
    if (mailboxUrl && localFolder)
    {
      rv = localFolder->GetFolderScanState(&folderScanState);
      NS_ENSURE_SUCCESS(rv, rv);
      nsCOMPtr <nsIMsgDBHdr> msgHdr;
      nsMsgKey msgKey;
      mailboxUrl->GetMessageKey(&msgKey);
      folder->GetMessageHeader(msgKey, getter_AddRefs(msgHdr));
      // we do this to get the account key
      if (msgHdr)
        localFolder->GetUidlFromFolder(&folderScanState, msgHdr);
      if (!folderScanState.m_accountKey.IsEmpty())
      {
        nsCOMPtr<nsIMsgAccountManager> accountManager =
                 do_GetService(NS_MSGACCOUNTMANAGER_CONTRACTID, &rv);
        if (accountManager)
        {
          nsCOMPtr <nsIMsgAccount> account;
          accountManager->GetAccount(folderScanState.m_accountKey, getter_AddRefs(account));
          if (account)
            account->GetIncomingServer(getter_AddRefs(server));
        }
      }
    }

    if (!server)
    rv = folder->GetServer(getter_AddRefs(server));
    if (NS_FAILED(rv)) return rv;
    nsCOMPtr<nsIPop3IncomingServer> popServer = do_QueryInterface(server,&rv);
    if (NS_FAILED(rv)) return rv;
    nsCString hostname;
    nsCString username;
    server->GetHostName(hostname);
    server->GetUsername(username);

    PRInt32 port;
    server->GetPort(&port);
    if (port == -1) port = nsIPop3URL::DEFAULT_POP3_PORT;

    // we need to escape the username because it may contain
    // characters like / % or @
    nsCString escapedUsername;
    MsgEscapeString(username, nsINetUtil::ESCAPE_XALPHAS, escapedUsername);

    nsCAutoString popSpec("pop://");
    popSpec += escapedUsername;
    popSpec += "@";
    popSpec += hostname;
    popSpec += ":";
    popSpec.AppendInt(port);
    popSpec += "?";
    popSpec += uidl;
    nsCOMPtr<nsIUrlListener> urlListener = do_QueryInterface(folder, &rv);
    if (NS_FAILED(rv)) return rv;
    rv = BuildPop3Url(popSpec.get(), folder, popServer,
                      urlListener, _retval, nsnull);
    if (NS_SUCCEEDED(rv))
    {
        nsCOMPtr<nsIMsgMailNewsUrl> mailnewsurl = do_QueryInterface(*_retval, &rv);
        if (NS_SUCCEEDED(rv))
        {
          // escape the username before we call SetUsername().  we do this because GetUsername()
          // will unescape the username
          mailnewsurl->SetUsername(escapedUsername);
        }
        nsCOMPtr<nsIPop3URL> popurl = do_QueryInterface(mailnewsurl, &rv);
        if (NS_SUCCEEDED(rv))
        {
          nsCAutoString messageUri (aSpec);
          if (!strncmp(messageUri.get(), "mailbox:", 8))
            messageUri.Replace(0, 8, "mailbox-message:");
          offset = messageUri.Find("?number=");
          if (offset != -1)
            messageUri.Replace(offset, 8, "#");
          offset = messageUri.Find("&");
          if (offset != -1)
            messageUri.SetLength(offset);
          popurl->SetMessageUri(messageUri.get());
          nsCOMPtr<nsIPop3Sink> pop3Sink;
          rv = popurl->GetPop3Sink(getter_AddRefs(pop3Sink));
          if (NS_SUCCEEDED(rv))
            pop3Sink->SetBuildMessageUri(PR_TRUE);
        }
    }
    return rv;
}

void nsPop3Service::AlertServerBusy(nsIMsgMailNewsUrl *url)
{
  nsresult rv;
  nsCOMPtr<nsIStringBundleService> bundleService(do_GetService("@mozilla.org/intl/stringbundle;1", &rv));
  if (NS_FAILED(rv))
    return;
  nsCOMPtr<nsIStringBundle> bundle;
  rv = bundleService->CreateBundle("chrome://messenger/locale/localMsgs.properties", getter_AddRefs(bundle));
  if (NS_FAILED(rv))
    return;
  nsCOMPtr<nsIMsgWindow> msgWindow;
  nsCOMPtr<nsIPrompt> dialog;
  rv = url->GetMsgWindow(getter_AddRefs(msgWindow)); //it is ok to have null msgWindow, for example when biffing
  if (NS_SUCCEEDED(rv) && msgWindow)
  {
    rv = msgWindow->GetPromptDialog(getter_AddRefs(dialog));
    if (NS_SUCCEEDED(rv))
    {
      nsString alertString;
      bundle->GetStringFromID(POP3_MESSAGE_FOLDER_BUSY, getter_Copies(alertString));
      if (!alertString.IsEmpty())
        dialog->Alert(nsnull, alertString.get());
    }
  }
}

NS_IMETHODIMP nsPop3Service::NewChannel(nsIURI *aURI, nsIChannel **_retval)
{
  NS_ENSURE_ARG_POINTER(aURI);
  nsresult rv = NS_OK;

  nsCOMPtr<nsIMsgMailNewsUrl> url = do_QueryInterface(aURI, &rv);
  nsCString realUserName;
  if (NS_SUCCEEDED(rv) && url)
  {
    nsCOMPtr <nsIMsgIncomingServer> server;
    url->GetServer(getter_AddRefs(server));
    if (server)
    {
      // find out if the server is busy or not...if the server is busy, we are
      // *NOT* going to run the url. The error code isn't quite right...
      // We might want to put up an error right here.
      PRBool serverBusy = PR_FALSE;
      rv = server->GetServerBusy(&serverBusy);
      if (serverBusy)
      {
        AlertServerBusy(url);
        return NS_MSG_FOLDER_BUSY;
      }
      server->GetRealUsername(realUserName);
    }
  }

  nsPop3Protocol * protocol = new nsPop3Protocol(aURI);
  if (protocol)
  {
    rv = protocol->Initialize(aURI);
    if (NS_FAILED(rv))
    {
      delete protocol;
      return rv;
    }
    protocol->SetUsername(realUserName.get());
    rv = protocol->QueryInterface(NS_GET_IID(nsIChannel), (void **) _retval);
  }
  else
    rv = NS_ERROR_NULL_POINTER;

  return rv;
}


NS_IMETHODIMP
nsPop3Service::SetDefaultLocalPath(nsILocalFile *aPath)
{
    NS_ENSURE_ARG(aPath);
    return NS_SetPersistentFile(PREF_MAIL_ROOT_POP3_REL, PREF_MAIL_ROOT_POP3, aPath);
}

NS_IMETHODIMP
nsPop3Service::GetDefaultLocalPath(nsILocalFile ** aResult)
{
    NS_ENSURE_ARG_POINTER(aResult);
    *aResult = nsnull;

    nsresult rv;
    PRBool havePref;
    nsCOMPtr<nsILocalFile> localFile;
    rv = NS_GetPersistentFile(PREF_MAIL_ROOT_POP3_REL,
                              PREF_MAIL_ROOT_POP3,
                              NS_APP_MAIL_50_DIR,
                              havePref,
                              getter_AddRefs(localFile));
        if (NS_FAILED(rv)) return rv;

    PRBool exists;
    rv = localFile->Exists(&exists);
    if (NS_SUCCEEDED(rv) && !exists)
        rv = localFile->Create(nsIFile::DIRECTORY_TYPE, 0775);
        if (NS_FAILED(rv)) return rv;

    if (!havePref || !exists) {
        rv = NS_SetPersistentFile(PREF_MAIL_ROOT_POP3_REL, PREF_MAIL_ROOT_POP3, localFile);
        NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to set root dir pref.");
    }

    NS_IF_ADDREF(*aResult = localFile);
    return NS_OK;
}


NS_IMETHODIMP
nsPop3Service::GetServerIID(nsIID* *aServerIID)
{
    *aServerIID = new nsIID(NS_GET_IID(nsIPop3IncomingServer));
    return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetRequiresUsername(PRBool *aRequiresUsername)
{
        NS_ENSURE_ARG_POINTER(aRequiresUsername);
        *aRequiresUsername = PR_TRUE;
        return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetPreflightPrettyNameWithEmailAddress(PRBool *aPreflightPrettyNameWithEmailAddress)
{
        NS_ENSURE_ARG_POINTER(aPreflightPrettyNameWithEmailAddress);
        *aPreflightPrettyNameWithEmailAddress = PR_TRUE;
        return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetCanLoginAtStartUp(PRBool *aCanLoginAtStartUp)
{
        NS_ENSURE_ARG_POINTER(aCanLoginAtStartUp);
        *aCanLoginAtStartUp = PR_TRUE;
        return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetCanDelete(PRBool *aCanDelete)
{
        NS_ENSURE_ARG_POINTER(aCanDelete);
        *aCanDelete = PR_TRUE;
        return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetCanDuplicate(PRBool *aCanDuplicate)
{
        NS_ENSURE_ARG_POINTER(aCanDuplicate);
        *aCanDuplicate = PR_TRUE;
        return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetCanGetMessages(PRBool *aCanGetMessages)
{
    NS_ENSURE_ARG_POINTER(aCanGetMessages);
    *aCanGetMessages = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetCanGetIncomingMessages(PRBool *aCanGetIncomingMessages)
{
    NS_ENSURE_ARG_POINTER(aCanGetIncomingMessages);
    *aCanGetIncomingMessages = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetShowComposeMsgLink(PRBool *showComposeMsgLink)
{
    NS_ENSURE_ARG_POINTER(showComposeMsgLink);
    *showComposeMsgLink = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::GetDefaultServerPort(PRBool isSecure, PRInt32 *aPort)
{
    NS_ENSURE_ARG_POINTER(aPort);
    nsresult rv = NS_OK;

    if (isSecure)
      *aPort = nsIPop3URL::DEFAULT_POP3S_PORT;
    else
      rv = GetDefaultPort(aPort);

    return rv;
}

NS_IMETHODIMP
nsPop3Service::GetSpecialFoldersDeletionAllowed(PRBool *specialFoldersDeletionAllowed)
{
    NS_ENSURE_ARG_POINTER(specialFoldersDeletionAllowed);
    *specialFoldersDeletionAllowed = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::NotifyDownloadStarted(nsIMsgFolder *aFolder)
{
  nsTObserverArray<nsCOMPtr<nsIPop3ServiceListener> >::ForwardIterator
    iter(mListeners);
  nsCOMPtr<nsIPop3ServiceListener> listener;
  while (iter.HasMore()) {
    listener = iter.GetNext();
    listener->OnDownloadStarted(aFolder);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::NotifyDownloadProgress(nsIMsgFolder *aFolder,
                                      PRUint32 aNumMessages,
                                      PRUint32 aNumTotalMessages)
{
  nsTObserverArray<nsCOMPtr<nsIPop3ServiceListener> >::ForwardIterator
    iter(mListeners);
  nsCOMPtr<nsIPop3ServiceListener> listener;
  while (iter.HasMore()) {
    listener = iter.GetNext();
    listener->OnDownloadProgress(aFolder, aNumMessages, aNumTotalMessages);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPop3Service::NotifyDownloadCompleted(nsIMsgFolder *aFolder,
                                       PRUint32 aNumMessages)
{
  nsTObserverArray<nsCOMPtr<nsIPop3ServiceListener> >::ForwardIterator
    iter(mListeners);
  nsCOMPtr<nsIPop3ServiceListener> listener;
  while (iter.HasMore()) {
    listener = iter.GetNext();
    listener->OnDownloadCompleted(aFolder, aNumMessages);
  }
  return NS_OK;
}

NS_IMETHODIMP nsPop3Service::AddListener(nsIPop3ServiceListener *aListener)
{
  NS_ENSURE_ARG_POINTER(aListener);
  mListeners.AppendElementUnlessExists(aListener);
  return NS_OK;
}

NS_IMETHODIMP nsPop3Service::RemoveListener(nsIPop3ServiceListener *aListener)
{
  NS_ENSURE_ARG_POINTER(aListener);
  mListeners.RemoveElement(aListener);
  return NS_OK;
}

