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

////////////////////////////////////////////////////////////////////////////////
// Core Module Include Files
////////////////////////////////////////////////////////////////////////////////
#include "nsCOMPtr.h"
#include "nsIModule.h"
#include "nsIGenericFactory.h"
#include "nsICategoryManager.h"
#include "nsServiceManagerUtils.h"

////////////////////////////////////////////////////////////////////////////////
// core import Include Files
////////////////////////////////////////////////////////////////////////////////
#include "nsImportService.h"
#include "nsImportMimeEncode.h"
#include "nsImportStringBundle.h"

////////////////////////////////////////////////////////////////////////////////
// text import Include Files
////////////////////////////////////////////////////////////////////////////////
#include "nsTextImport.h"

static NS_DEFINE_CID(kTextImportCID,      NS_TEXTIMPORT_CID);

////////////////////////////////////////////////////////////////////////////////
// nsComm4x import Include Files
////////////////////////////////////////////////////////////////////////////////
#include "nsComm4xProfile.h"
#include "nsComm4xMailStringBundle.h"
#include "nsComm4xMailImport.h"

static NS_DEFINE_CID(kComm4xMailImportCID,      NS_COMM4XMAILIMPORT_CID);

////////////////////////////////////////////////////////////////////////////////
// eudora import Include Files
////////////////////////////////////////////////////////////////////////////////
#if defined(XP_WIN) || defined(XP_MACOSX)
#include "nsEudoraImport.h"
#include "nsEudoraStringBundle.h"

static NS_DEFINE_CID(kEudoraImportCID,      NS_EUDORAIMPORT_CID);
#endif

////////////////////////////////////////////////////////////////////////////////
// Apple Mail import Include Files
////////////////////////////////////////////////////////////////////////////////
#if defined(XP_MACOSX)
#include "nsAppleMailImport.h"

static NS_DEFINE_CID(kAppleMailImportCID,      NS_APPLEMAILIMPORT_CID);
#endif

////////////////////////////////////////////////////////////////////////////////
// outlook import Include Files
////////////////////////////////////////////////////////////////////////////////
#ifdef XP_WIN
#if defined(_MSC_VER) && _MSC_VER >= 1100
#include "nsOEImport.h"
#include "nsOEStringBundle.h"
#include "nsOutlookImport.h"
#include "nsOutlookStringBundle.h"
#include "nsWMImport.h"
#include "nsWMStringBundle.h"

static NS_DEFINE_CID(kOEImportCID,         NS_OEIMPORT_CID);
static NS_DEFINE_CID(kOutlookImportCID,      NS_OUTLOOKIMPORT_CID);
static NS_DEFINE_CID(kWMImportCID,         NS_WMIMPORT_CID);
#endif

#endif // XP_WIN

////////////////////////////////////////////////////////////////////////////////
// core import factories
////////////////////////////////////////////////////////////////////////////////
NS_GENERIC_FACTORY_CONSTRUCTOR(nsImportService)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsIImportMimeEncodeImpl)

////////////////////////////////////////////////////////////////////////////////
// text import factories
////////////////////////////////////////////////////////////////////////////////
NS_GENERIC_FACTORY_CONSTRUCTOR(nsTextImport)

NS_METHOD TextRegister(nsIComponentManager *aCompMgr,
                       nsIFile *aPath, const char *registryLocation,
                       const char *componentType,
                       const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService( NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED( rv)) {
    nsCString replace;
    char *theCID = kTextImportCID.ToString();
    rv = catMan->AddCategoryEntry( "mailnewsimport", theCID, kTextSupportsString, PR_TRUE, PR_TRUE, getter_Copies( replace));
    NS_Free( theCID);
  }

  return( rv);
}

////////////////////////////////////////////////////////////////////////////////
// nsComm4x import factories
////////////////////////////////////////////////////////////////////////////////
NS_GENERIC_FACTORY_CONSTRUCTOR(nsComm4xMailImport)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(ImportComm4xMailImpl, Initialize)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsComm4xProfile)

NS_METHOD Comm4xMailRegister(nsIComponentManager *aCompMgr,
                       nsIFile *aPath, const char *registryLocation,
                       const char *componentType,
                       const nsModuleComponentInfo *info)
{
    nsresult rv;

    nsCOMPtr<nsICategoryManager> catMan = do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv)) {
        nsCString  replace;
        char *theCID = kComm4xMailImportCID.ToString();
        rv = catMan->AddCategoryEntry("mailnewsimport", theCID, kComm4xMailSupportsString, PR_TRUE, PR_TRUE, getter_Copies(replace));
        NS_Free(theCID);
    }

    return rv;
}

////////////////////////////////////////////////////////////////////////////////
// eudora import factories
////////////////////////////////////////////////////////////////////////////////
#if defined(XP_WIN) || defined(XP_MACOSX)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsEudoraImport)

NS_METHOD EudoraRegister(nsIComponentManager *aCompMgr,
                         nsIFile *aPath, const char *registryLocation,
                         const char *componentType,
                         const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService( NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED( rv)) {
    nsCString replace;
    char *theCID = kEudoraImportCID.ToString();
    rv = catMan->AddCategoryEntry( "mailnewsimport", theCID, kEudoraSupportsString, PR_TRUE, PR_TRUE, getter_Copies( replace));
    NS_Free( theCID);
  }

  return( rv);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// apple mail import factories
////////////////////////////////////////////////////////////////////////////////
#if defined(XP_MACOSX)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsAppleMailImportModule)
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsAppleMailImportMail, Initialize)

NS_METHOD AppleMailRegister(nsIComponentManager *aCompMgr,
                         nsIFile *aPath, const char *registryLocation,
                         const char *componentType,
                         const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService( NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED( rv)) {
    nsCString replace;
    char *theCID = kAppleMailImportCID.ToString();
    rv = catMan->AddCategoryEntry( "mailnewsimport", theCID, kAppleMailSupportsString, PR_TRUE, PR_TRUE, getter_Copies( replace));
    NS_Free( theCID);
  }

  return( rv);
}
#endif

////////////////////////////////////////////////////////////////////////////////
// outlook import factories
////////////////////////////////////////////////////////////////////////////////
#ifdef XP_WIN

#if defined(_MSC_VER) && _MSC_VER >= 1100

NS_GENERIC_FACTORY_CONSTRUCTOR(nsOEImport)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsOutlookImport)
NS_GENERIC_FACTORY_CONSTRUCTOR(nsWMImport)

NS_METHOD OERegister(nsIComponentManager *aCompMgr,
                     nsIFile *aPath,
                     const char *registryLocation,
                     const char *componentType,
                     const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService( NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED( rv)) {
    nsCString replace;
    char *theCID = kOEImportCID.ToString();
    rv = catMan->AddCategoryEntry( "mailnewsimport", theCID, kOESupportsString, PR_TRUE, PR_TRUE, getter_Copies( replace));
    NS_Free( theCID);
  }

    return( rv);
}

NS_METHOD WMRegister(nsIComponentManager *aCompMgr,
                     nsIFile *aPath,
                     const char *registryLocation,
                     const char *componentType,
                     const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService(NS_CATEGORYMANAGER_CONTRACTID,
                                                      &rv);
  if (NS_SUCCEEDED(rv)) {
    nsCString replace;
    char *theCID = kWMImportCID.ToString();
    rv = catMan->AddCategoryEntry("mailnewsimport", theCID, kWMSupportsString,
                                  PR_TRUE, PR_TRUE, getter_Copies(replace));
    NS_Free(theCID);
  }

    return( rv);
}

NS_METHOD OutlookRegister(nsIComponentManager *aCompMgr,
                          nsIFile *aPath, const char *registryLocation,
                          const char *componentType,
                          const nsModuleComponentInfo *info)
{
  nsresult rv;

  nsCOMPtr<nsICategoryManager> catMan = do_GetService( NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_SUCCEEDED( rv)) {
    nsCString replace;
    char *theCID = kOutlookImportCID.ToString();
    rv = catMan->AddCategoryEntry( "mailnewsimport", theCID, kOutlookSupportsString, PR_TRUE, PR_TRUE, getter_Copies( replace));
    NS_Free( theCID);
  }

  return( rv);
}
#endif

#endif // XP_WIN

static const nsModuleComponentInfo components[] = {
    ////////////////////////////////////////////////////////////////////////////////
    // core import components
    ////////////////////////////////////////////////////////////////////////////////
    { "Import Service Component", NS_IMPORTSERVICE_CID,
        NS_IMPORTSERVICE_CONTRACTID, nsImportServiceConstructor },
    { "Import Mime Encoder", NS_IMPORTMIMEENCODE_CID,
      "@mozilla.org/import/import-mimeencode;1", nsIImportMimeEncodeImplConstructor},

    ////////////////////////////////////////////////////////////////////////////////
    // text import components
    ////////////////////////////////////////////////////////////////////////////////
    { "Text Import Component", NS_TEXTIMPORT_CID,
      "@mozilla.org/import/import-text;1", nsTextImportConstructor, TextRegister, nsnull },

    ////////////////////////////////////////////////////////////////////////////////
    // nsComm4x import components
    ////////////////////////////////////////////////////////////////////////////////
    { "Comm4xMail Import Component", NS_COMM4XMAILIMPORT_CID,
      "@mozilla.org/import/import-comm4xMail;1", nsComm4xMailImportConstructor, Comm4xMailRegister, nsnull },
    { "Comm4xMail Import Mail Implementation", NS_COMM4XMAILIMPL_CID,
      NS_COMM4XMAILIMPL_CONTRACTID, ImportComm4xMailImplConstructor},
    { NS_ICOMM4XPROFILE_CLASSNAME, NS_ICOMM4XPROFILE_CID,
      NS_ICOMM4XPROFILE_CONTRACTID, nsComm4xProfileConstructor }

    ////////////////////////////////////////////////////////////////////////////////
    // eduora import components
    ////////////////////////////////////////////////////////////////////////////////
#if defined(XP_WIN) || defined(XP_MACOSX)
    ,{ "Text Import Component", NS_EUDORAIMPORT_CID,
    "@mozilla.org/import/import-eudora;1", nsEudoraImportConstructor, EudoraRegister, nsnull }
#endif

    ////////////////////////////////////////////////////////////////////////////////
    // apple mail import components
    ////////////////////////////////////////////////////////////////////////////////
#if defined(XP_MACOSX)
        ,{ "Apple Mail Import Component", NS_APPLEMAILIMPORT_CID,
        "@mozilla.org/import/import-applemail;1", nsAppleMailImportModuleConstructor, AppleMailRegister, nsnull },
        { "Apple Mail Import Implementation", NS_APPLEMAILIMPL_CID,
          NS_APPLEMAILIMPL_CONTRACTID, nsAppleMailImportMailConstructor },
#endif

#ifdef XP_WIN
#if defined(_MSC_VER) && _MSC_VER >= 1100
    ////////////////////////////////////////////////////////////////////////////////
    // oexpress import components
    ////////////////////////////////////////////////////////////////////////////////
    ,{ "Outlook Express Import Component", NS_OEIMPORT_CID,
    "@mozilla.org/import/import-oe;1", nsOEImportConstructor,  OERegister,  nsnull },
    { "Windows Live Mail Import Component", NS_WMIMPORT_CID,
    "@mozilla.org/import/import-wm;1", nsWMImportConstructor,  WMRegister,  nsnull },
    { "Outlook Import Component", NS_OUTLOOKIMPORT_CID,
    "@mozilla.org/import/import-outlook;1", nsOutlookImportConstructor, OutlookRegister, nsnull }
#endif
#endif

};


static void importModuleDtor(nsIModule* self)
{
#if defined(XP_WIN) || defined(XP_MACOSX)
    nsEudoraStringBundle::Cleanup();
#endif

#ifdef XP_WIN

#if defined(_MSC_VER) && _MSC_VER >= 1100
    nsOEStringBundle::Cleanup();
    nsWMStringBundle::Cleanup();
    nsOutlookStringBundle::Cleanup();
#endif

#endif
}

NS_IMPL_NSGETMODULE_WITH_DTOR( nsImportServiceModule, components, importModuleDtor)


