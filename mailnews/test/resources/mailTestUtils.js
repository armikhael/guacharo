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
 * Kent James <kent@caspia.com>.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mark Banner <bugzilla@standard8.plus.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

// Make sure we execute this file exactly once
var gMailTestUtils_js__;
if (!gMailTestUtils_js__) {
gMailTestUtils_js__ = true;

// we would like for everyone to have fixIterator and toXPComArray
Components.utils.import("resource:///modules/iteratorUtils.jsm");
// exposes component loader's btoa impl
Components.utils.import("resource:///modules/IOUtils.js");

// Local Mail Folders. Requires prior setup of profile directory

var gLocalIncomingServer;
var gLocalInboxFolder;

function loadLocalMailAccount()
{
  var acctMgr = Cc["@mozilla.org/messenger/account-manager;1"]
                  .getService(Ci.nsIMsgAccountManager);
  acctMgr.createLocalMailAccount();

  gLocalIncomingServer = acctMgr.localFoldersServer;

  var rootFolder = gLocalIncomingServer.rootMsgFolder;

  // Note: Inbox is not created automatically when there is no deferred server,
  // so we need to create it.
  gLocalInboxFolder = rootFolder.addSubfolder("Inbox");
  // a local inbox should have a Mail flag!
  gLocalInboxFolder.setFlag(Ci.nsMsgFolderFlags.Mail);

  // Force an initialization of the Inbox folder database.
  var folderName = gLocalInboxFolder.prettiestName;
}

/**
 * atob() = base64 decode
 * Converts a base64-encoded string to a string with the octet data.
 * @see RFC 4648
 *
 * The extra parameters are optional arguments that are used to override the
 * official base64 characters for values 62 and 63. If not specified, they
 * default to '+' and '/'.
 *
 * No unicode translation is performed during the conversion.
 *
 * @param str    A string argument representing the encoded data
 * @param c62    The (optional) character for the value 62
 * @param c63    The (optional) character for the value 63
 * @return       An string with the data
 */
function atob(str, c62, c63) {
  var result = [];
  var bits = [];
  c62 = c62 ? c62.charCodeAt(0) : 43;
  c63 = c63 ? c63.charCodeAt(0) : 47;
  for (var i=0;i<str.length;i++) {
    let c = str.charCodeAt(i);
    let val = 0;
    if (65 <= c && c <= 90) // A-Z
      val = c-65;
    else if (97 <= c && c <= 122) // a-z
      val = c-97+26;
    else if (48 <= c && c <= 57) // 0-9
      val = c-48+52;
    else if (c == c62)
      val = 62;
    else if (c == c63)
      val = 63;
    else if (c == 61) {
      for (var q=i+1;q<str.length;q++)
        if (str[q] != '=')
          throw "Character after =: "+str[q];
      break;
    } else
      throw "Illegal character in input: "+c;
    bits.push((val >> 5) & 1);
    bits.push((val >> 4) & 1);
    bits.push((val >> 3) & 1);
    bits.push((val >> 2) & 1);
    bits.push((val >> 1) & 1);
    bits.push((val >> 0) & 1);
    if (bits.length >= 8)
      result.push(bits.splice(0, 8).reduce(function (form, bit) {
        return (form << 1) | bit;
      }, 0));
  }
  return result.reduce(function (str, c) { return str + String.fromCharCode(c); }, "");
}

/*
 * We used to implement btoa here, but we don't need to as it's provided by
 * the JS loader and theirs is ridiculously faster!.  We have IOUtils expose it
 * for us so we can get at it.
 */
var btoa = IOUtils.btoa;

// Loads a file to a string
// If aCharset is specified, treats the file as being of that charset
function loadFileToString(aFile, aCharset) {
  var data = "";
  var fstream = Cc["@mozilla.org/network/file-input-stream;1"]
                  .createInstance(Ci.nsIFileInputStream);
  fstream.init(aFile, -1, 0, 0);

  if (aCharset)
  {
    var cstream = Cc["@mozilla.org/intl/converter-input-stream;1"]
                    .createInstance(Ci.nsIConverterInputStream);
    cstream.init(fstream, aCharset, 4096, 0x0000);
    var str = {};
    while (cstream.readString(4096, str) != 0)
      data += str.value;

    cstream.close();
  }
  else
  {
    var sstream = Cc["@mozilla.org/scriptableinputstream;1"]
                    .createInstance(Ci.nsIScriptableInputStream);

    sstream.init(fstream);

    var str = sstream.read(4096);
    while (str.length > 0) {
      data += str;
      str = sstream.read(4096);
    }

    sstream.close();
  }

  fstream.close();

  return data;
}

/**
 * A variant of do_timeout that accepts an actual function instead of
 *  requiring you to pass a string to evaluate.  If the function throws an
 *  exception when invoked, we will use do_throw to ensure that the test fails.
 *
 * @param aDelayInMS The number of milliseconds to wait before firing the timer.
 * @param aFunc The function to invoke when the timer fires.
 * @param aFuncThis Optional 'this' pointer to use.
 * @param aFuncArgs Optional list of arguments to pass to the function.
 */
function do_timeout_function(aDelayInMS, aFunc, aFuncThis, aFuncArgs) {
  let timer = Components.classes["@mozilla.org/timer;1"]
                        .createInstance(Components.interfaces.nsITimer);
  let wrappedFunc = function() {
    try {
      aFunc.apply(aFuncThis, aFuncArgs);
    }
    catch (ex) {
      // we want to make sure that if the thing we call throws an exception,
      //  that this terminates the test.
      do_throw(ex);
    }
  }
  timer.initWithCallback(wrappedFunc, aDelayInMS,
    Components.interfaces.nsITimer.TYPE_ONE_SHOT);
}

/**
 * Ensure the given nsIMsgFolder's database is up-to-date, calling the provided
 *  callback once the folder has been loaded.  (This may be instantly or
 *  after a re-parse.)
 *
 * @param aFolder The nsIMsgFolder whose database you want to ensure is
 *     up-to-date.
 * @param aCallback The callback function to invoke once the folder has been
 *     loaded.
 * @param aCallbackThis The 'this' to use when calling the callback.  Pass null
 *     if your callback does not rely on 'this'.
 * @param aCallbackArgs A list of arguments to pass to the callback via apply.
 *     If you provide [1,2,3], we will effectively call:
 *     aCallbackThis.aCallback(1,2,3);
 * @param [aSomeoneElseWillTriggerTheUpdate=false] If this is true, we do not
 *     trigger the updateFolder call and it is assumed someone else is taking
 *     care of that.
 */
function updateFolderAndNotify(aFolder, aCallback, aCallbackThis,
    aCallbackArgs, aSomeoneElseWillTriggerTheUpdate) {
  // register for the folder loaded notification ahead of time... even though
  //  we may not need it...
  let mailSession = Cc["@mozilla.org/messenger/services/session;1"]
                      .getService(Ci.nsIMsgMailSession);
  let atomService = Cc["@mozilla.org/atom-service;1"]
                      .getService(Ci.nsIAtomService);
  let kFolderLoadedAtom = atomService.getAtom("FolderLoaded");

  let folderListener = {
    OnItemEvent: function (aEventFolder, aEvent) {
      if (aEvent == kFolderLoadedAtom && aFolder.URI == aEventFolder.URI) {
        mailSession.RemoveFolderListener(this);
        aCallback.apply(aCallbackThis, aCallbackArgs);
      }
    }
  };

  mailSession.AddFolderListener(folderListener, Ci.nsIFolderListener.event);

  if (!aSomeoneElseWillTriggerTheUpdate)
    aFolder.updateFolder(null);
}

} // gMailTestUtils_js__
