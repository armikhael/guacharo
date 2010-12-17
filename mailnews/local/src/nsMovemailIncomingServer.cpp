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
 *   Adam D. Moss <adam@gimp.org>
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

#include "nsMsgLocalCID.h"
#include "nsMsgFolderFlags.h"
#include "nsIMsgLocalMailFolder.h"
#include "nsIMovemailService.h"
#include "nsIFile.h"
#include "msgCore.h" // pre-compiled headers
#include "nsMovemailIncomingServer.h"
#include "nsServiceManagerUtils.h"


static NS_DEFINE_CID(kCMovemailServiceCID, NS_MOVEMAILSERVICE_CID);


NS_IMPL_ISUPPORTS_INHERITED2(nsMovemailIncomingServer,
                             nsMsgIncomingServer,
                             nsIMovemailIncomingServer,
                             nsILocalMailIncomingServer)

                            

nsMovemailIncomingServer::nsMovemailIncomingServer()
{    
    m_canHaveFilters = PR_TRUE;
}

nsMovemailIncomingServer::~nsMovemailIncomingServer()
{
}

NS_IMETHODIMP
nsMovemailIncomingServer::GetIsSecureServer(PRBool *aIsSecureServer)
{
    NS_ENSURE_ARG_POINTER(aIsSecureServer);
    *aIsSecureServer = PR_FALSE;
    return NS_OK;
}

NS_IMETHODIMP 
nsMovemailIncomingServer::PerformBiff(nsIMsgWindow *aMsgWindow)
{
    nsresult rv;
    nsCOMPtr<nsIMovemailService> movemailService(do_GetService(
                                                 kCMovemailServiceCID, &rv));
    if (NS_FAILED(rv)) return rv;
    nsCOMPtr<nsIMsgFolder> inbox;
    nsCOMPtr<nsIMsgFolder> rootMsgFolder;
    nsCOMPtr<nsIUrlListener> urlListener;
    rv = GetRootMsgFolder(getter_AddRefs(rootMsgFolder));
    if(NS_SUCCEEDED(rv) && rootMsgFolder)
    {
         rootMsgFolder->GetFolderWithFlags(nsMsgFolderFlags::Inbox,
                                           getter_AddRefs(inbox));
         if (!inbox) return NS_ERROR_FAILURE;
    }

    SetPerformingBiff(PR_TRUE);
    urlListener = do_QueryInterface(inbox);

    PRBool downloadOnBiff = PR_FALSE;
    rv = GetDownloadOnBiff(&downloadOnBiff);
    if (downloadOnBiff)
    {
       nsCOMPtr <nsIMsgLocalMailFolder> localInbox = do_QueryInterface(inbox,
                                                                       &rv);
       if (localInbox && NS_SUCCEEDED(rv))
       {
           PRBool valid = PR_FALSE;
           nsCOMPtr <nsIMsgDatabase> db;
           rv = inbox->GetMsgDatabase(getter_AddRefs(db));
           if (NS_SUCCEEDED(rv) && db)
           {
               rv = db->GetSummaryValid(&valid);
           }
           if (NS_SUCCEEDED(rv) && valid)
           {
               rv = movemailService->GetNewMail(aMsgWindow, urlListener, inbox,
                                                this, nsnull);
           }
           else
           {
              PRBool isLocked;
              inbox->GetLocked(&isLocked);
              if (!isLocked)
              {
                 rv = localInbox->ParseFolder(aMsgWindow, urlListener);
              }
              if (NS_SUCCEEDED(rv))
              {
                 rv = localInbox->SetCheckForNewMessagesAfterParsing(PR_TRUE);
              }
           }
       }
    }
    else
    {
        movemailService->CheckForNewMail(urlListener, inbox, this, nsnull); 
    }

    return NS_OK;
}

NS_IMETHODIMP
nsMovemailIncomingServer::SetFlagsOnDefaultMailboxes()
{
    nsCOMPtr<nsIMsgFolder> rootFolder;
    nsresult rv = GetRootFolder(getter_AddRefs(rootFolder));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIMsgLocalMailFolder> localFolder =
        do_QueryInterface(rootFolder, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    localFolder->SetFlagsOnDefaultMailboxes(nsMsgFolderFlags::Inbox |
                                            nsMsgFolderFlags::Archive |
                                            nsMsgFolderFlags::SentMail |
                                            nsMsgFolderFlags::Drafts |
                                            nsMsgFolderFlags::Templates |
                                            nsMsgFolderFlags::Trash |
                                            nsMsgFolderFlags::Junk |
                                            // hmm?
                                            nsMsgFolderFlags::Queue);
    return NS_OK;
}

NS_IMETHODIMP nsMovemailIncomingServer::CreateDefaultMailboxes(nsIFile *aPath)
{
    NS_ENSURE_ARG_POINTER(aPath);
    nsCOMPtr <nsIFile> path;
    nsresult rv = aPath->Clone(getter_AddRefs(path));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = path->AppendNative(NS_LITERAL_CSTRING("Inbox"));
    if (NS_FAILED(rv)) return rv;
    PRBool exists;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) 
    {
      rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
      if (NS_FAILED(rv)) return rv;
    }

    rv = path->SetNativeLeafName(NS_LITERAL_CSTRING("Trash"));
    if (NS_FAILED(rv)) return rv;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) {
        rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
        if (NS_FAILED(rv)) return rv;
    }

    rv = path->SetNativeLeafName(NS_LITERAL_CSTRING("Sent"));
    if (NS_FAILED(rv)) return rv;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) {
        rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
        if (NS_FAILED(rv)) return rv;
    }

    rv = path->SetNativeLeafName(NS_LITERAL_CSTRING("Drafts"));
    if (NS_FAILED(rv)) return rv;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) {
        rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
        if (NS_FAILED(rv)) return rv;
    }

    rv = path->SetNativeLeafName(NS_LITERAL_CSTRING("Templates"));
    if (NS_FAILED(rv)) return rv;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) {
        rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
        if (NS_FAILED(rv)) return rv;
    }

    rv = path->SetNativeLeafName(NS_LITERAL_CSTRING("Unsent Messages"));
    if (NS_FAILED(rv)) return rv;
    rv = path->Exists(&exists);
    if (NS_FAILED(rv)) return rv;
    if (!exists) {
        rv = path->Create(nsIFile::NORMAL_FILE_TYPE, 0644);
        if (NS_FAILED(rv)) return rv;
    }
    return rv;
}


NS_IMETHODIMP
nsMovemailIncomingServer::GetNewMail(nsIMsgWindow *aMsgWindow,
                                     nsIUrlListener *aUrlListener,
                                     nsIMsgFolder *aMsgFolder,
                                     nsIURI **aResult)
{
    nsresult rv;
    
    nsCOMPtr<nsIMovemailService> movemailService = 
             do_GetService(kCMovemailServiceCID, &rv);
    
    if (NS_FAILED(rv)) return rv;
    
    rv = movemailService->GetNewMail(aMsgWindow, aUrlListener,
                                     aMsgFolder, this, aResult);

    return rv;
}        

NS_IMETHODIMP
nsMovemailIncomingServer::GetDownloadMessagesAtStartup(PRBool *getMessagesAtStartup)
{
    NS_ENSURE_ARG_POINTER(getMessagesAtStartup);
    *getMessagesAtStartup = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsMovemailIncomingServer::GetCanBeDefaultServer(PRBool *aCanBeDefaultServer)
{
  NS_ENSURE_ARG_POINTER(aCanBeDefaultServer);
  *aCanBeDefaultServer = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
nsMovemailIncomingServer::GetCanSearchMessages(PRBool *canSearchMessages)
{
    NS_ENSURE_ARG_POINTER(canSearchMessages);
    *canSearchMessages = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP 
nsMovemailIncomingServer::GetServerRequiresPasswordForBiff(PRBool *aServerRequiresPasswordForBiff)
{
    NS_ENSURE_ARG_POINTER(aServerRequiresPasswordForBiff);
    *aServerRequiresPasswordForBiff = PR_FALSE;
    return NS_OK;
}

NS_IMETHODIMP 
nsMovemailIncomingServer::GetAccountManagerChrome(nsAString& aResult)
{
    aResult.AssignLiteral("am-main.xul");
    return NS_OK;
}
