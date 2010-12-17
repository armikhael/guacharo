// Import the main scripts that mailnews tests need to set up and tear down
load("../../mailnews/resources/mailDirService.js");
load("../../mailnews/resources/mailTestUtils.js");

// Import the pop3 server scripts
load("../../mailnews/fakeserver/maild.js")
load("../../mailnews/fakeserver/auth.js")
load("../../mailnews/fakeserver/pop3d.js")

const POP3_PORT = 1024+110;

// Setup the daemon and server
// If the debugOption is set, then it will be applied to the server.
function setupServerDaemon(debugOption) {
  var daemon = new pop3Daemon();
  var handler = new POP3_RFC5034_handler(daemon);
  var server = new nsMailServer(handler);
  if (debugOption)
    server.setDebugLevel(debugOption);
  return [daemon, server, handler];
}

function createPop3ServerAndLocalFolders() {
  loadLocalMailAccount();

  var acctMgr = Cc["@mozilla.org/messenger/account-manager;1"]
                  .getService(Ci.nsIMsgAccountManager);

  var incoming = acctMgr.createIncomingServer("fred", "localhost", "pop3");

  incoming.port = POP3_PORT;
  incoming.password = "wilma";

  return incoming;
}

function do_check_transaction(real, expected) {
  // If we don't spin the event loop before starting the next test, the readers
  // aren't expired. In this case, the "real" real transaction is the last one.
  if (real instanceof Array)
    real = real[real.length - 1];

  // real.them may have an extra QUIT on the end, where the stream is only
  // closed after we have a chance to process it and not them. We therefore
  // excise this from the list
  if (real.them[real.them.length-1] == "QUIT")
    real.them.pop();

  do_check_eq(real.them.join(","), expected.join(","));
  dump("Passed test " + test + "\n");
}
