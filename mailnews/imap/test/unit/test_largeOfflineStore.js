/* -*- Mode: JavaScript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Test to ensure that downloadAllForOffline works correctly for large imap
 * stores, i.e., > 4GB.
 */

var gIMAPDaemon, gServer, gIMAPIncomingServer;

const gIMAPService = Cc["@mozilla.org/messenger/messageservice;1?type=imap"]
                       .getService(Ci.nsIMsgMessageService);

load("../../mailnews/resources/messageGenerator.js");

var gDownloadedOnce = false;
var gIMAPInbox;
var gOfflineStoreSize;

function run_test()
{
  loadLocalMailAccount();

  /*
   * Set up an IMAP server.
   */
  gIMAPDaemon = new imapDaemon();
  gServer = makeServer(gIMAPDaemon, "");
  gIMAPIncomingServer = createLocalIMAPServer();
  gIMAPIncomingServer.maximumConnectionsNumber = 1;

  // pref tuning: one connection only, turn off notifications
  let prefBranch = Cc["@mozilla.org/preferences-service;1"]
                     .getService(Ci.nsIPrefBranch);
  prefBranch.setBoolPref("mail.biff.play_sound", false);
  prefBranch.setBoolPref("mail.biff.show_alert", false);
  prefBranch.setBoolPref("mail.biff.show_tray_icon", false);
  prefBranch.setBoolPref("mail.biff.animate_dock_icon", false);

  // On Windows, check whether the drive is NTFS. If it is, mark the file as
  // sparse. If it isn't, then bail out now, because in all probability it is
  // FAT32, which doesn't support file sizes greater than 4 GB.
  if ("@mozilla.org/windows-registry-key;1" in Cc)
  {
    // Figure out the name of the IMAP inbox
    let file = gIMAPIncomingServer.rootMsgFolder.filePath.clone();
    file.append("INBOX");
    if (!file.exists())
      file.create(Ci.nsIFile.NORMAL_FILE_TYPE, 0644);

    // Now call upon our helper
    let helper = do_get_cwd().clone();
    // '../../mailnews/LargeOfflineStoreHelper.exe'.
    helper = helper.parent.parent;
    helper.normalize();
    helper.append("mailnews");
    helper.append("LargeOfflineStoreHelper.exe");
    if (!helper.exists()) {
      do_throw(helper.leafName + " not found");
    }

    let helperProc = Cc["@mozilla.org/process/util;1"]
                       .createInstance(Ci.nsIProcess);
    helperProc.init(helper);
    let args = [file.path];
    // XXX This needs to be fixed to use runw once it lands (bug 411511)
    helperProc.run(true, args, args.length);
    let exitValue = helperProc.exitValue;

    // 0 is success, 1 is "unable to run," and any other value is failure
    if (exitValue == 1)
    {
      dump("On Windows, this test only works on NTFS volumes.\n");
      endTest();
      return;
    }

    if (exitValue != 0)
    {
      do_throw(helper.leafName + " failed with exit value " + exitValue +
                 ", see above for details");
    }
  }

  let inbox = gIMAPDaemon.getMailbox("INBOX");

  let ioService = Cc["@mozilla.org/network/io-service;1"]
                    .getService(Ci.nsIIOService);

  // "Master" do_test_pending(), paired with a do_test_finished() at the end of
  // all the operations.
  do_test_pending();

  // Create a couple test messages.
  let messages = [];
  let messageGenerator = new MessageGenerator();
  let scenarioFactory = new MessageScenarioFactory(messageGenerator);

  messages = messages.concat(scenarioFactory.directReply(2));
  let dataUri = ioService.newURI("data:text/plain;base64," +
                   btoa(messages[0].toMessageString()),
                        null, null);
  let imapMsg = new imapMessage(dataUri.spec, inbox.uidnext++, []);
  inbox.addMessage(imapMsg);

  dataUri = ioService.newURI("data:text/plain;base64," +
                   btoa(messages[1].toMessageString()),
                        null, null);
  imapMsg = new imapMessage(dataUri.spec, inbox.uidnext++, []);
  inbox.addMessage(imapMsg);

  // Get the IMAP inbox...
  let rootFolder = gIMAPIncomingServer.rootFolder;
  let freeDiskSpace = rootFolder.filePath.diskSpaceAvailable;

  // On Windows, the file is marked as sparse above. Linux file systems
  // generally support sparse files automatically. OS X's HFS+, however, doesn't
  // support sparse files, so check for at least 8 GB of free disk space before
  // consuming 4 GB.
  if ("nsILocalFileMac" in Ci && freeDiskSpace < 0x200000000) {
    dump("On MacOSX, this test needs 8 GB of free disk space.\n");
    endTest();
    return;
  }

  gIMAPInbox = rootFolder.getFolderWithFlags(Ci.nsMsgFolderFlags.Inbox);
  let outputStream = gIMAPInbox.offlineStoreOutputStream
                               .QueryInterface(Ci.nsISeekableStream);
  // seek to 15 bytes past 4GB.
  outputStream.seek(0, 0x10000000f);
  outputStream.write("from\r\n", 6);
  outputStream.close();
  gOfflineStoreSize = gIMAPInbox.filePath.fileSize;
  // ...and download for offline use.
  gIMAPInbox.downloadAllForOffline(UrlListener, null);
}

var UrlListener =
{
  OnStartRunningUrl: function(url) { },
  OnStopRunningUrl: function(url, rc)
  {
    // Check for ok status.
    do_check_eq(rc, 0);

    if (!gDownloadedOnce) {
      gDownloadedOnce = true;
      gIMAPInbox.downloadAllForOffline(UrlListener, null);
      return;
    }
    else {
      // verify that the message headers have the offline flag set.
      let msgEnumerator = gIMAPInbox.msgDatabase.EnumerateMessages();
      let offset = new Object;
      let size = new Object;
      while (msgEnumerator.hasMoreElements())
      {
        let header = msgEnumerator.getNext();
        // Verify that each message has been downloaded and looks OK.
        if (header instanceof Components.interfaces.nsIMsgDBHdr &&
            (header.flags & Ci.nsMsgMessageFlags.Offline))
          gIMAPInbox.getOfflineFileStream(header.messageKey, offset, size).close();
        else
          do_throw("Message not downloaded for offline use");

        dump("msg hdr offset = " + offset.value + "\n");
      }
      let offlineStoreSize = gIMAPInbox.filePath.fileSize;
      dump("offline store size = " + offlineStoreSize + "\n");
      // Make sure offline store grew (i.e., we're not writing over data).
      do_check_true(offlineStoreSize > gOfflineStoreSize);
      // free up disk space - if you want to look at the file after running
      // this test, comment out this line.
      gIMAPInbox.filePath.remove(false);
    }

    try {
      do_timeout(1000, endTest);
    } catch(ex) {
      do_throw(ex);
    }
  }
};

function endTest()
{
  gIMAPIncomingServer.closeCachedConnections();
  gServer.stop();

  var thread = gThreadManager.currentThread;
  while (thread.hasPendingEvents())
    thread.processNextEvent(true);

  do_test_finished();
}
