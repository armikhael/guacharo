/* Tests internal URIs generated by various methods in the code base.
 * If you manually generate a news URI somewhere, please add it to this test.
 */

load("../../../resources/logHelper.js");
load("../../../resources/asyncTestUtils.js");
load("../../../resources/alertTestUtils.js");

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

let dummyMsgWindow = {
  get statusFeedback() {
    return {
      startMeteors: function () {},
      stopMeteors: function () {
        async_driver();
      },
      showProgress: function () {}
    };
  },
  get promptDialog() {
    return alertUtilsPrompts;
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIMsgWindow,
                                         Ci.nsISupportsWeakReference])
};
var daemon, localserver, server;

var nntpService = Cc["@mozilla.org/messenger/nntpservice;1"]
                    .getService(Components.interfaces.nsINntpService);

let tests = [
  test_newMsgs,
  test_cancel,
  test_fetchMessage,
  test_search,
  test_grouplist,
  test_postMessage,
  test_escapedName,
  cleanUp
];

function test_newMsgs() {
  // This tests nsMsgNewsFolder::GetNewsMessages via getNewMessages
  let folder = localserver.rootFolder.getChildNamed("test.filter");
  do_check_eq(folder.getTotalMessages(false), 0);
  folder.getNewMessages(null, asyncUrlListener);
  yield false;
  do_check_eq(folder.getTotalMessages(false), 8);
  yield true;
}

// Prompts for cancel
function alert(title, text) {}
function confirmEx(title, text, flags) {  return 0; }

function test_cancel() {
  // This tests nsMsgNewsFolder::CancelMessage
  let folder = localserver.rootFolder.getChildNamed("test.filter");
  let db = folder.msgDatabase;
  let hdr = db.GetMsgHdrForKey(4);

  let mailSession = Cc['@mozilla.org/messenger/services/session;1']
                      .getService(Ci.nsIMsgMailSession);
  let atomService = Cc['@mozilla.org/atom-service;1']
                      .getService(Ci.nsIAtomService);
  let kDeleteAtom = atomService.getAtom("DeleteOrMoveMsgCompleted");
  let folderListener = {
    OnItemEvent: function(aEventFolder, aEvent) {
      if (aEvent == kDeleteAtom) {
        mailSession.RemoveFolderListener(this);
      }
    }
  };
  mailSession.AddFolderListener(folderListener, Ci.nsIFolderListener.event);
  folder.QueryInterface(Ci.nsIMsgNewsFolder)
        .cancelMessage(hdr, dummyMsgWindow);
  yield false;

  do_check_eq(folder.getTotalMessages(false), 7);
  yield true;
}

function test_fetchMessage() {
  // Tests nsNntpService::CreateMessageIDURL via FetchMessage
  var statuscode = -1;
  let streamlistener = {
    onDataAvailable: function() {},
    onStartRequest: function() {
    },
    onStopRequest: function (aRequest, aContext, aStatus) {
      statuscode = aStatus;
    },
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIStreamListener,
                                           Ci.nsIRequestObserver])
  };
  let folder = localserver.rootFolder.getChildNamed("test.filter");
  nntpService.fetchMessage(folder, 2, null, streamlistener, asyncUrlListener);
  yield false;
  do_check_eq(statuscode, Components.results.NS_OK);
  yield true;
}

function test_search() {
  // This tests nsNntpService::Search
  let folder = localserver.rootFolder.getChildNamed("test.filter");
  var searchSession = Cc["@mozilla.org/messenger/searchSession;1"]
                        .createInstance(Ci.nsIMsgSearchSession);
  searchSession.addScopeTerm(Ci.nsMsgSearchScope.news, folder);

  let searchTerm = searchSession.createTerm();
  searchTerm.attrib = Ci.nsMsgSearchAttrib.Subject;
  let value = searchTerm.value;
  value.str = 'First';
  searchTerm.value = value;
  searchTerm.op = Ci.nsMsgSearchOp.Contains;
  searchTerm.booleanAnd = false;
  searchSession.appendTerm(searchTerm);

  let hitCount;
  let searchListener = {
    onSearchHit: function (dbHdr, folder) { hitCount++; },
    onSearchDone: function (status) {
      searchSession.unregisterListener(this);
      async_driver();
    },
    onNewSearch: function() { hitCount = 0; }
  };
  searchSession.registerListener(searchListener);

  searchSession.search(null);
  yield false;

  do_check_eq(hitCount, 1);
  yield true;
}

function test_grouplist() {
  // This tests nsNntpService::GetListOfGroupsOnServer
  let subserver = localserver.QueryInterface(Ci.nsISubscribableServer);
  let subscribeListener = {
    OnDonePopulating: function () { async_driver(); }
  };
  subserver.subscribeListener = subscribeListener;

  function enumGroups(rootUri) {
    let hierarchy = subserver.getChildren(rootUri);
    let groups = [];
    while (hierarchy.hasMoreElements()) {
      let element = hierarchy.getNext().QueryInterface(Ci.nsIRDFResource);
      let name = element.ValueUTF8;
      name = name.slice(name.lastIndexOf("/") + 1);
      if (subserver.isSubscribable(name))
        groups.push(name);
      if (subserver.hasChildren(name))
        groups = groups.concat(enumGroups(name));
    }
    return groups;
  }

  nntpService.getListOfGroupsOnServer(localserver, null, false);
  yield false;

  let groups = enumGroups("");
  for (let group in daemon._groups)
    do_check_true(groups.indexOf(group) >= 0);
  yield true;
}

function test_postMessage() {
  // This tests nsNntpService::SetUpNntpUrlForPosting via PostMessage
  nntpService.postMessage(do_get_file("postings/post2.eml"), "misc.test",
    localserver.key, asyncUrlListener, null);
  yield false;
  do_check_eq(daemon.getGroup("misc.test").keys.length, 1);
  yield true;
}

// Not tested because it requires UI, and this is insufficient, I think.
function test_forwardInline() {
  // This tests mime_parse_stream_complete via forwarding inline
  let composeSvc = Cc['@mozilla.org/messengercompose;1']
                     .getService(Ci.nsIMsgComposeService);
  let folder = localserver.rootFolder.getChildNamed("test.filter");
  let hdr = folder.msgDatabase.GetMsgHdrForKey(1);
  composeSvc.forwardMessage("a@b.c", hdr, null,
    localserver, Ci.nsIMsgComposeService.kForwardInline);
}

function test_escapedName() {
  // This does a few tests to make sure our internal URIs work for newsgroups
  // with names that need escaping
  let evilName = "test.malformed&name";
  daemon.addGroup(evilName);
  daemon.addArticle(make_article(do_get_file("postings/bug670935.eml")));
  localserver.subscribeToNewsgroup(evilName);

  // Can we access it?
  let folder = localserver.rootFolder.getChildNamed(evilName);
  folder.getNewMessages(null, asyncUrlListener);
  yield false;

  // If we get here, we didn't crash--newsgroups unescape properly.
  // Load a message, to test news-message: URI unescaping
  var statuscode = -1;
  let streamlistener = {
    onDataAvailable: function() {},
    onStartRequest: function() {
    },
    onStopRequest: function (aRequest, aContext, aStatus) {
      statuscode = aStatus;
    },
    QueryInterface: XPCOMUtils.generateQI([Ci.nsIStreamListener,
                                           Ci.nsIRequestObserver])
  };
  nntpService.fetchMessage(folder, 1, null, streamlistener, asyncUrlListener);
  yield false;
  do_check_eq(statuscode, Components.results.NS_OK);
  yield true;
}

function run_test() {
  daemon = setupNNTPDaemon();
  localserver = setupLocalServer(NNTP_PORT);
  server = makeServer(NNTP_RFC2980_handler, daemon);
  server.start(NNTP_PORT);
  server.setDebugLevel(fsDebugAll);

  // Set up an identity for posting
  var acctmgr = Cc["@mozilla.org/messenger/account-manager;1"]
                  .getService(Ci.nsIMsgAccountManager);
  let identity = acctmgr.createIdentity();
  identity.fullName = "Normal Person";
  identity.email = "fake@acme.invalid";
  acctmgr.FindAccountForServer(localserver).addIdentity(identity);

  async_run_tests(tests);
}

function cleanUp() {
  localserver.closeCachedConnections();
}
