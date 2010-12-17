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
 *   Lorenzo Colitti <lorenzo@colitti.com>
 *   Hans-Andreas Engel <engel@physics.harvard.edu>
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

#ifdef MOZ_LOGGING
// sorry, this has to be before the pre-compiled header
#define FORCE_PR_LOG /* Allow logging in the release build */
#endif

#include "msgCore.h"  // for pre-compiled headers
#include "nsMimeTypes.h"
#include "nsImapCore.h"
#include "nsImapProtocol.h"
#include "nsImapServerResponseParser.h"
#include "nsIMAPBodyShell.h"
#include "nsImapFlagAndUidState.h"
#include "nsIMAPNamespace.h"
#include "nsImapStringBundle.h"
#include "nsImapUtils.h"
#include "nsMsgUtils.h"

////////////////// nsImapServerResponseParser /////////////////////////

extern PRLogModuleInfo* IMAP;

nsImapServerResponseParser::nsImapServerResponseParser(nsImapProtocol &imapProtocolConnection) 
                            : nsIMAPGenericParser(),
    fReportingErrors(PR_TRUE),
    fCurrentFolderReadOnly(PR_FALSE),
    fCurrentLineContainedFlagInfo(PR_FALSE),
    fFetchEverythingRFC822(PR_FALSE),
    fServerIsNetscape3xServer(PR_FALSE),
    fNumberOfUnseenMessages(0),
    fNumberOfExistingMessages(0),
    fNumberOfRecentMessages(0),
    fSizeOfMostRecentMessage(0),
    fTotalDownloadSize(0),
    fCurrentCommandTag(nsnull),
    fSelectedMailboxName(nsnull),
    fIMAPstate(kNonAuthenticated),
    fLastChunk(PR_FALSE),
    m_shell(nsnull),
    fServerConnection(imapProtocolConnection),
    fHostSessionList(nsnull)
{
  fSearchResults = nsImapSearchResultSequence::CreateSearchResultSequence();
  fFolderAdminUrl = nsnull;
  fNetscapeServerVersionString = nsnull;
  fXSenderInfo = nsnull;
  fSupportsUserDefinedFlags = 0;
  fSettablePermanentFlags = 0;
  fCapabilityFlag = kCapabilityUndefined; 
  fLastAlert = nsnull;
  fDownloadingHeaders = PR_FALSE;
  fGotPermanentFlags = PR_FALSE;
  fFolderUIDValidity = 0;
  fHighestModSeq = 0;
  fAuthChallenge = nsnull;
  fStatusUnseenMessages = 0;
  fStatusRecentMessages = 0;
  fStatusNextUID = nsMsgKey_None;
  fStatusExistingMessages = 0;
  fReceivedHeaderOrSizeForUID = nsMsgKey_None;
  fCondStoreEnabled = PR_FALSE;
}

nsImapServerResponseParser::~nsImapServerResponseParser()
{
  PR_Free( fCurrentCommandTag );
  delete fSearchResults; 
  PR_Free( fFolderAdminUrl );
  PR_Free( fNetscapeServerVersionString );
  PR_Free( fXSenderInfo );
  PR_Free( fLastAlert );
  PR_Free( fSelectedMailboxName );
  PR_Free(fAuthChallenge);

  NS_IF_RELEASE (fHostSessionList);
}

PRBool nsImapServerResponseParser::LastCommandSuccessful()
{
  return (!CommandFailed() && 
    !fServerConnection.DeathSignalReceived() &&
    nsIMAPGenericParser::LastCommandSuccessful());
}

// returns PR_TRUE if things look ok to continue
PRBool nsImapServerResponseParser::GetNextLineForParser(char **nextLine)
{
  PRBool rv = PR_TRUE;
  *nextLine = fServerConnection.CreateNewLineFromSocket();
  if (fServerConnection.DeathSignalReceived() ||
      NS_FAILED(fServerConnection.GetConnectionStatus()))
    rv = PR_FALSE;
  // we'd really like to try to silently reconnect, but we shouldn't put this
  // message up just in the interrupt case
  if (NS_FAILED(fServerConnection.GetConnectionStatus()) &&
      !fServerConnection.DeathSignalReceived())
    fServerConnection.AlertUserEventUsingId(IMAP_SERVER_DISCONNECTED);
  return rv;
}

PRBool nsImapServerResponseParser::CommandFailed()
{
  return fCurrentCommandFailed;
}

void nsImapServerResponseParser::SetCommandFailed(PRBool failed)
{
  fCurrentCommandFailed = failed;
}

void nsImapServerResponseParser::SetFlagState(nsIImapFlagAndUidState *state)
{
  fFlagState = state;
}

PRInt32 nsImapServerResponseParser::SizeOfMostRecentMessage()
{
  return fSizeOfMostRecentMessage;
}

// Call this when adding a pipelined command to the session
void nsImapServerResponseParser::IncrementNumberOfTaggedResponsesExpected(const char *newExpectedTag)
{
  fNumberOfTaggedResponsesExpected++;
  PR_Free( fCurrentCommandTag );
  fCurrentCommandTag = PL_strdup(newExpectedTag);
  if (!fCurrentCommandTag)
    HandleMemoryFailure();
}

void nsImapServerResponseParser::InitializeState()
{
  fCurrentCommandFailed = PR_FALSE;
  fNumberOfRecentMessages = 0;
  fReceivedHeaderOrSizeForUID = nsMsgKey_None;
}

// RFC3501:  response = *(continue-req / response-data) response-done
//           response-data = "*" SP (resp-cond-state / resp-cond-bye /
//                           mailbox-data / message-data / capability-data) CRLF
//           continue-req    = "+" SP (resp-text / base64) CRLF
void nsImapServerResponseParser::ParseIMAPServerResponse(const char *aCurrentCommand,
                                                         PRBool aIgnoreBadAndNOResponses,
                                                         char *aGreetingWithCapability)
{
  
  NS_ASSERTION(aCurrentCommand && *aCurrentCommand != '\r' && 
    *aCurrentCommand != '\n' && *aCurrentCommand != ' ', "Invailid command string");
  PRBool sendingIdleDone = !strcmp(aCurrentCommand, "DONE"CRLF);
  if (sendingIdleDone)
    fWaitingForMoreClientInput = PR_FALSE;

  // Reinitialize the parser
  SetConnected(PR_TRUE);
  SetSyntaxError(PR_FALSE);
  
  // Reinitialize our state
  InitializeState();
  
  // the default is to not pipeline
  fNumberOfTaggedResponsesExpected = 1;
  int numberOfTaggedResponsesReceived = 0;
  
  nsCString copyCurrentCommand(aCurrentCommand);
  if (!fServerConnection.DeathSignalReceived())
  {
    char *placeInTokenString = nsnull;
    char *tagToken = nsnull;
    const char *commandToken = nsnull;
    PRBool inIdle = PR_FALSE;
    if (!sendingIdleDone)
    {
      placeInTokenString = copyCurrentCommand.BeginWriting();
      tagToken = NS_strtok(WHITESPACE, &placeInTokenString);
      commandToken = NS_strtok(WHITESPACE, &placeInTokenString);
    }
    else
      commandToken = "DONE";
    if (tagToken)
    {
      PR_Free( fCurrentCommandTag );
      fCurrentCommandTag = PL_strdup(tagToken);
      if (!fCurrentCommandTag)
        HandleMemoryFailure();
      inIdle = commandToken && !strcmp(commandToken, "IDLE");
    }
    
    if (commandToken && ContinueParse())
      PreProcessCommandToken(commandToken, aCurrentCommand);
    
    if (ContinueParse())
    {
      ResetLexAnalyzer();

      if (aGreetingWithCapability)
      {
        PR_FREEIF(fCurrentLine);
        fCurrentLine = aGreetingWithCapability;
      }

      do {
        AdvanceToNextToken();

        // untagged responses [RFC3501, Sec. 2.2.2]
        while (ContinueParse() && !PL_strcmp(fNextToken, "*") )
        {
          response_data();
          if (ContinueParse())
          {
            if (!fAtEndOfLine)
              SetSyntaxError(PR_TRUE);
            else if (!inIdle && !fCurrentCommandFailed && !aGreetingWithCapability)
              AdvanceToNextToken();
          }
        }

        // command continuation request [RFC3501, Sec. 7.5]
        if (ContinueParse() && *fNextToken == '+')	// never pipeline APPEND or AUTHENTICATE
        {
          NS_ASSERTION((fNumberOfTaggedResponsesExpected - numberOfTaggedResponsesReceived) == 1, 
            " didn't get the number of tagged responses we expected");
          numberOfTaggedResponsesReceived = fNumberOfTaggedResponsesExpected;
          if (commandToken && !PL_strcasecmp(commandToken, "authenticate") && placeInTokenString && 
            (!PL_strncasecmp(placeInTokenString, "CRAM-MD5", strlen("CRAM-MD5"))
             || !PL_strncasecmp(placeInTokenString, "NTLM", strlen("NTLM"))
             || !PL_strncasecmp(placeInTokenString, "GSSAPI", strlen("GSSAPI"))
             || !PL_strncasecmp(placeInTokenString, "MSN", strlen("MSN"))))
          {
            // we need to store the challenge from the server if we are using CRAM-MD5 or NTLM. 
            authChallengeResponse_data();
          }
        }
        else
          numberOfTaggedResponsesReceived++;
        
        if (numberOfTaggedResponsesReceived < fNumberOfTaggedResponsesExpected)
          response_tagged();
        
      } while (ContinueParse() && !inIdle && (numberOfTaggedResponsesReceived < fNumberOfTaggedResponsesExpected));
      
      // check and see if the server is waiting for more input
      // it's possible that we ate this + while parsing certain responses (like cram data),
      // in these cases, the parsing routine for that specific command will manually set
      // fWaitingForMoreClientInput so we don't lose that information....
      if (*fNextToken == '+' || inIdle)
      {
        fWaitingForMoreClientInput = PR_TRUE;
      }
      // if we aren't still waiting for more input....
      else if (!fWaitingForMoreClientInput && !aGreetingWithCapability)
      {
        if (ContinueParse())
          response_done();
        
        if (ContinueParse() && !CommandFailed())
        {
          // a successful command may change the eIMAPstate
          ProcessOkCommand(commandToken);
        }
        else if (CommandFailed())
        {
          // a failed command may change the eIMAPstate
          ProcessBadCommand(commandToken);
          if (fReportingErrors && !aIgnoreBadAndNOResponses)
            fServerConnection.AlertUserEventFromServer(fCurrentLine);
        }
      }
    }
  }
  else
    SetConnected(PR_FALSE);
}

void nsImapServerResponseParser::HandleMemoryFailure()
{
  fServerConnection.AlertUserEventUsingId(IMAP_OUT_OF_MEMORY);
  nsIMAPGenericParser::HandleMemoryFailure();
}


// SEARCH is the only command that requires pre-processing for now.
// others will be added here.
void nsImapServerResponseParser::PreProcessCommandToken(const char *commandToken,
                                                        const char *currentCommand)
{
  fCurrentCommandIsSingleMessageFetch = PR_FALSE;
  fWaitingForMoreClientInput = PR_FALSE;
  
  if (!PL_strcasecmp(commandToken, "SEARCH"))
    fSearchResults->ResetSequence();
  else if (!PL_strcasecmp(commandToken, "SELECT") && currentCommand)
  {
    // the mailbox name must be quoted, so strip the quotes
    const char *openQuote = PL_strchr(currentCommand, '"');
    NS_ASSERTION(openQuote, "expected open quote in imap server response");
    if (!openQuote)
    { // ill formed select command
      openQuote = PL_strchr(currentCommand, ' ');
    }
    PR_Free( fSelectedMailboxName);
    fSelectedMailboxName = PL_strdup(openQuote + 1);
    if (fSelectedMailboxName)
    {
      // strip the escape chars and the ending quote
      char *currentChar = fSelectedMailboxName;
      while (*currentChar)
      {
        if (*currentChar == '\\')
        {
          PL_strcpy(currentChar, currentChar+1);
          currentChar++;	// skip what we are escaping
        }
        else if (*currentChar == '\"')
          *currentChar = 0;	// end quote
        else
          currentChar++;
      }
    }
    else
      HandleMemoryFailure();
    
    // we don't want bogus info for this new box
    //delete fFlagState;	// not our object
    //fFlagState = nsnull;
  }
  else if (!PL_strcasecmp(commandToken, "CLOSE"))
  {
    return;	// just for debugging
    // we don't want bogus info outside the selected state
    //delete fFlagState;	// not our object
    //fFlagState = nsnull;
  }
  else if (!PL_strcasecmp(commandToken, "UID"))
  {
    nsCString copyCurrentCommand(currentCommand);
    if (!fServerConnection.DeathSignalReceived())
    {
      char *placeInTokenString = copyCurrentCommand.BeginWriting();
      char *tagToken = NS_strtok(WHITESPACE, &placeInTokenString);
      char *uidToken = NS_strtok(WHITESPACE, &placeInTokenString);
      char *fetchToken = NS_strtok(WHITESPACE, &placeInTokenString);
      uidToken = nsnull; // use variable to quiet compiler warning
      tagToken = nsnull; // use variable to quiet compiler warning
      if (!PL_strcasecmp(fetchToken, "FETCH") )
      {
        char *uidStringToken = NS_strtok(WHITESPACE, &placeInTokenString);
        // , and : are uid delimiters
        if (!PL_strchr(uidStringToken, ',') && !PL_strchr(uidStringToken, ':'))
          fCurrentCommandIsSingleMessageFetch = PR_TRUE;
      }
    }
  }
}

const char *nsImapServerResponseParser::GetSelectedMailboxName()
{
  return fSelectedMailboxName;
}

nsImapSearchResultIterator *nsImapServerResponseParser::CreateSearchResultIterator()
{
  return new nsImapSearchResultIterator(*fSearchResults);
}

nsImapServerResponseParser::eIMAPstate nsImapServerResponseParser::GetIMAPstate()
{
  return fIMAPstate;
}

void nsImapServerResponseParser::PreauthSetAuthenticatedState()
{
  fIMAPstate = kAuthenticated;
}

void nsImapServerResponseParser::ProcessOkCommand(const char *commandToken)
{
  if (!PL_strcasecmp(commandToken, "LOGIN") ||
    !PL_strcasecmp(commandToken, "AUTHENTICATE"))
    fIMAPstate = kAuthenticated;
  else if (!PL_strcasecmp(commandToken, "LOGOUT"))
    fIMAPstate = kNonAuthenticated;
  else if (!PL_strcasecmp(commandToken, "SELECT") ||
    !PL_strcasecmp(commandToken, "EXAMINE"))
    fIMAPstate = kFolderSelected;
  else if (!PL_strcasecmp(commandToken, "CLOSE"))
  {
    fIMAPstate = kAuthenticated;
    // we no longer have a selected mailbox.
    PR_FREEIF( fSelectedMailboxName );
  }
  else if ((!PL_strcasecmp(commandToken, "LIST")) ||
          (!PL_strcasecmp(commandToken, "LSUB")) ||
          (!PL_strcasecmp(commandToken, "XLIST")))
  {
    //fServerConnection.MailboxDiscoveryFinished();
    // This used to be reporting that we were finished
    // discovering folders for each time we issued a
    // LIST or LSUB.  So if we explicitly listed the
    // INBOX, or Trash, or namespaces, we would get multiple
    // "done" states, even though we hadn't finished.
    // Move this to be called from the connection object
    // itself.
  }
  else if (!PL_strcasecmp(commandToken, "FETCH"))
  {
    if (!fZeroLengthMessageUidString.IsEmpty())
    {
      // "Deleting zero length message");
      fServerConnection.Store(fZeroLengthMessageUidString, "+Flags (\\Deleted)", PR_TRUE);
      if (LastCommandSuccessful())
        fServerConnection.Expunge();
      
      fZeroLengthMessageUidString.Truncate();
    }
  }
  if (GetFillingInShell())
  {
    // There is a BODYSTRUCTURE response.  Now let's generate the stream...
    // that is, if we're not doing it already
    if (!m_shell->IsBeingGenerated())
    {
      nsImapProtocol *navCon = &fServerConnection;
      
      char *imapPart = nsnull;
      
      fServerConnection.GetCurrentUrl()->GetImapPartToFetch(&imapPart);
      m_shell->Generate(imapPart);
      PR_Free(imapPart);
      
      if ((navCon && navCon->GetPseudoInterrupted())
        || fServerConnection.DeathSignalReceived())
      {
        // we were pseudointerrupted or interrupted
        if (!m_shell->IsShellCached())
        {
          // if it's not in the cache, then we were (pseudo)interrupted while generating
          // for the first time.  Delete it.
          delete m_shell;
        }
        navCon->PseudoInterrupt(PR_FALSE);
      }
      else if (m_shell->GetIsValid())
      {
        // If we have a valid shell that has not already been cached, then cache it.
        if (!m_shell->IsShellCached() && fHostSessionList)	// cache is responsible for destroying it
        {
          PR_LOG(IMAP, PR_LOG_ALWAYS, 
            ("BODYSHELL:  Adding shell to cache."));
          const char *serverKey = fServerConnection.GetImapServerKey();
          fHostSessionList->AddShellToCacheForHost(
            serverKey, m_shell);
        }
      }
      else
      {
        // The shell isn't valid, so we don't cache it.
        // Therefore, we have to destroy it here.
        delete m_shell;
      }
      m_shell = nsnull;
    }
  }
}

void nsImapServerResponseParser::ProcessBadCommand(const char *commandToken)
{
  if (!PL_strcasecmp(commandToken, "LOGIN") ||
    !PL_strcasecmp(commandToken, "AUTHENTICATE"))
    fIMAPstate = kNonAuthenticated;
  else if (!PL_strcasecmp(commandToken, "LOGOUT"))
    fIMAPstate = kNonAuthenticated;	// ??
  else if (!PL_strcasecmp(commandToken, "SELECT") ||
    !PL_strcasecmp(commandToken, "EXAMINE"))
    fIMAPstate = kAuthenticated;	// nothing selected
  else if (!PL_strcasecmp(commandToken, "CLOSE"))
    fIMAPstate = kAuthenticated;	// nothing selected
  if (GetFillingInShell())
  {
    if (!m_shell->IsBeingGenerated())
    {
      delete m_shell;
      m_shell = nsnull;
    }
  }
}



// RFC3501:  response-data = "*" SP (resp-cond-state / resp-cond-bye /
//                           mailbox-data / message-data / capability-data) CRLF
// These are ``untagged'' responses [RFC3501, Sec. 2.2.2]
/*
 The RFC1730 grammar spec did not allow one symbol look ahead to determine
 between mailbox_data / message_data so I combined the numeric possibilities
 of mailbox_data and all of message_data into numeric_mailbox_data.
 
 It is assumed that the initial "*" is already consumed before calling this
 method. The production implemented here is 
         response_data   ::= (resp_cond_state / resp_cond_bye /
                              mailbox_data / numeric_mailbox_data / 
                              capability_data)
                              CRLF
*/
void nsImapServerResponseParser::response_data()
{
  AdvanceToNextToken();
  
  if (ContinueParse())
  {
    // Instead of comparing lots of strings and make function calls, try to
    // pre-flight the possibilities based on the first letter of the token.
    switch (toupper(fNextToken[0]))
    {
    case 'O':   // OK
      if (toupper(fNextToken[1]) == 'K')
        resp_cond_state(PR_FALSE);
      else SetSyntaxError(PR_TRUE);
      break;
    case 'N':   // NO
      if (toupper(fNextToken[1]) == 'O')
        resp_cond_state(PR_FALSE);
      else if (!PL_strcasecmp(fNextToken, "NAMESPACE"))
        namespace_data();
      else SetSyntaxError(PR_TRUE);
      break;
    case 'B':   // BAD
      if (!PL_strcasecmp(fNextToken, "BAD"))
        resp_cond_state(PR_FALSE);
      else if (!PL_strcasecmp(fNextToken, "BYE"))
        resp_cond_bye();
      else SetSyntaxError(PR_TRUE);
      break;
    case 'F':
      if (!PL_strcasecmp(fNextToken, "FLAGS"))
        mailbox_data();
      else SetSyntaxError(PR_TRUE);
      break;
    case 'P':
      if (PL_strcasecmp(fNextToken, "PERMANENTFLAGS"))
        mailbox_data();
      else SetSyntaxError(PR_TRUE);
      break;
    case 'L':
      if (!PL_strcasecmp(fNextToken, "LIST") || !PL_strcasecmp(fNextToken, "LSUB"))
        mailbox_data();
      else if (!PL_strcasecmp(fNextToken, "LANGUAGE"))
        language_data();
      else
        SetSyntaxError(PR_TRUE);
      break;
    case 'M':
      if (!PL_strcasecmp(fNextToken, "MAILBOX"))
        mailbox_data();
      else if (!PL_strcasecmp(fNextToken, "MYRIGHTS"))
        myrights_data(PR_FALSE);
      else SetSyntaxError(PR_TRUE);
      break;
    case 'S':
      if (!PL_strcasecmp(fNextToken, "SEARCH"))
        mailbox_data();
      else if (!PL_strcasecmp(fNextToken, "STATUS"))
      {
        AdvanceToNextToken();
        if (fNextToken)
        {
          char *mailboxName = CreateAstring();
          PL_strfree( mailboxName); 
        }
        while (	ContinueParse() &&
          !fAtEndOfLine )
        {
          AdvanceToNextToken();
          if (!fNextToken)
            break;
          
          if (*fNextToken == '(') fNextToken++;
          if (!PL_strcasecmp(fNextToken, "UIDNEXT"))
          {
            AdvanceToNextToken();
            if (fNextToken)
            {
              fStatusNextUID = atoi(fNextToken);
              // if this token ends in ')', then it is the last token
              // else we advance
              if ( *(fNextToken + strlen(fNextToken) - 1) == ')')
                fNextToken += strlen(fNextToken) - 1;
            }
          }
          else if (!PL_strcasecmp(fNextToken, "MESSAGES"))
          {
            AdvanceToNextToken();
            if (fNextToken)
            {
              fStatusExistingMessages = atoi(fNextToken);
              // if this token ends in ')', then it is the last token
              // else we advance
              if ( *(fNextToken + strlen(fNextToken) - 1) == ')')
                fNextToken += strlen(fNextToken) - 1;
            }
          }
          else if (!PL_strcasecmp(fNextToken, "UNSEEN"))
          {
            AdvanceToNextToken();
            if (fNextToken)
            {
              fStatusUnseenMessages = atoi(fNextToken);
              // if this token ends in ')', then it is the last token
              // else we advance
              if ( *(fNextToken + strlen(fNextToken) - 1) == ')')
                fNextToken += strlen(fNextToken) - 1;
            }
          }
          else if (!PL_strcasecmp(fNextToken, "RECENT"))
          {
            AdvanceToNextToken();
            if (fNextToken)
            {
              fStatusRecentMessages = atoi(fNextToken);
              // if this token ends in ')', then it is the last token
              // else we advance
              if ( *(fNextToken + strlen(fNextToken) - 1) == ')')
                fNextToken += strlen(fNextToken) - 1;
            }
          }
          else if (*fNextToken == ')')
            break;
          else if (!fAtEndOfLine)
            SetSyntaxError(PR_TRUE);
        } 
      } else SetSyntaxError(PR_TRUE);
      break;
    case 'C':
      if (!PL_strcasecmp(fNextToken, "CAPABILITY"))
        capability_data();
      else SetSyntaxError(PR_TRUE);
      break;
    case 'V':
      if (!PL_strcasecmp(fNextToken, "VERSION"))
      {
        // figure out the version of the Netscape server here
        PR_FREEIF(fNetscapeServerVersionString);
        AdvanceToNextToken();
        if (! fNextToken) 
          SetSyntaxError(PR_TRUE);
        else
        {
          fNetscapeServerVersionString = CreateAstring();
          AdvanceToNextToken();
          if (fNetscapeServerVersionString)
          {
            fServerIsNetscape3xServer = (*fNetscapeServerVersionString == '3');
          }
        }
        skip_to_CRLF();
      }
      else SetSyntaxError(PR_TRUE);
      break;
    case 'A':
      if (!PL_strcasecmp(fNextToken, "ACL"))
      {
        acl_data();
      }
      else if (!PL_strcasecmp(fNextToken, "ACCOUNT-URL"))
      {
        fMailAccountUrl.Truncate();
        AdvanceToNextToken();
        if (! fNextToken) 
          SetSyntaxError(PR_TRUE);
        else
        {
          fMailAccountUrl.Adopt(CreateAstring());
          AdvanceToNextToken();
        }
      } 
      else SetSyntaxError(PR_TRUE);
      break;
    case 'E':
      if (!PL_strcasecmp(fNextToken, "ENABLED"))
        enable_data();
        break;
    case 'X':
      if (!PL_strcasecmp(fNextToken, "XSERVERINFO"))
        xserverinfo_data();
      else if (!PL_strcasecmp(fNextToken, "XMAILBOXINFO"))
        xmailboxinfo_data();
      else if (!PL_strcasecmp(fNextToken, "XAOL-OPTION"))
        skip_to_CRLF();
      else if (!PL_strcasecmp(fNextToken, "XLIST"))
        mailbox_data();
      else 
      {
        // check if custom command
        nsCAutoString customCommand;
        fServerConnection.GetCurrentUrl()->GetCommand(customCommand);
        if (customCommand.Equals(fNextToken))
        {
          nsCAutoString customCommandResponse;
          while (Connected() && !fAtEndOfLine)
          {
            AdvanceToNextToken();
            customCommandResponse.Append(fNextToken);
            customCommandResponse.Append(" ");
          }
          fServerConnection.GetCurrentUrl()->SetCustomCommandResult(customCommandResponse);
        }
        else
          SetSyntaxError(PR_TRUE);
      }
      break;
    case 'Q':
      if (!PL_strcasecmp(fNextToken, "QUOTAROOT")  || !PL_strcasecmp(fNextToken, "QUOTA"))
        quota_data();
      else
        SetSyntaxError(PR_TRUE);
      break;
    default:
      if (IsNumericString(fNextToken))
        numeric_mailbox_data();
      else
        SetSyntaxError(PR_TRUE);
      break;
    }
    
    if (ContinueParse())
      PostProcessEndOfLine();
  }
}


void nsImapServerResponseParser::PostProcessEndOfLine()
{
  // for now we only have to do one thing here
  // a fetch response to a 'uid store' command might return the flags
  // before it returns the uid of the message.  So we need both before
  // we report the new flag info to the front end
  
  // also check and be sure that there was a UID in the current response
  if (fCurrentLineContainedFlagInfo && CurrentResponseUID())
  {
    fCurrentLineContainedFlagInfo = PR_FALSE;
    fServerConnection.NotifyMessageFlags(fSavedFlagInfo, CurrentResponseUID(), fHighestModSeq);
  }
}


/*
 mailbox_data    ::=  "FLAGS" SPACE flag_list /
                               "LIST" SPACE mailbox_list /
                               "LSUB" SPACE mailbox_list /
                               "XLIST" SPACE mailbox_list /
                               "MAILBOX" SPACE text /
                               "SEARCH" [SPACE 1#nz_number] /
                               number SPACE "EXISTS" / number SPACE "RECENT"

This production was changed to accomodate predictive parsing 

 mailbox_data    ::=  "FLAGS" SPACE flag_list /
                               "LIST" SPACE mailbox_list /
                               "LSUB" SPACE mailbox_list /
                               "XLIST" SPACE mailbox_list /
                               "MAILBOX" SPACE text /
                               "SEARCH" [SPACE 1#nz_number]
*/
void nsImapServerResponseParser::mailbox_data()
{
  if (!PL_strcasecmp(fNextToken, "FLAGS")) 
  {
    // this handles the case where we got the permanent flags response
    // before the flags response, in which case, we want to ignore thes flags.
    if (fGotPermanentFlags)
      skip_to_CRLF();
    else
      parse_folder_flags();
  }
  else if (!PL_strcasecmp(fNextToken, "LIST") ||
           !PL_strcasecmp(fNextToken, "XLIST"))
  {
    AdvanceToNextToken();
    if (ContinueParse())
      mailbox_list(PR_FALSE);
  }
  else if (!PL_strcasecmp(fNextToken, "LSUB"))
  {
    AdvanceToNextToken();
    if (ContinueParse())
      mailbox_list(PR_TRUE);
  }
  else if (!PL_strcasecmp(fNextToken, "MAILBOX"))
    skip_to_CRLF();
  else if (!PL_strcasecmp(fNextToken, "SEARCH"))
  {
    fSearchResults->AddSearchResultLine(fCurrentLine);
    fServerConnection.NotifySearchHit(fCurrentLine);
    skip_to_CRLF();
  }
}

/*
 mailbox_list    ::= "(" #("\Marked" / "\Noinferiors" /
                              "\Noselect" / "\Unmarked" / flag_extension) ")"
                              SPACE (<"> QUOTED_CHAR <"> / nil) SPACE mailbox
*/

void nsImapServerResponseParser::mailbox_list(PRBool discoveredFromLsub)
{
  nsImapMailboxSpec *boxSpec = new nsImapMailboxSpec;
  NS_ADDREF(boxSpec);
  PRBool needsToFreeBoxSpec = PR_TRUE;
  if (!boxSpec)
    HandleMemoryFailure();
  else
  {
    boxSpec->mFolderSelected = PR_FALSE;
    boxSpec->mBoxFlags = kNoFlags;
    boxSpec->mAllocatedPathName.Truncate();
    boxSpec->mHostName.Truncate();
    boxSpec->mConnection = &fServerConnection;
    boxSpec->mFlagState = nsnull;
    boxSpec->mDiscoveredFromLsub = discoveredFromLsub;
    boxSpec->mOnlineVerified = PR_TRUE;
    boxSpec->mBoxFlags &= ~kNameSpace;
    
    PRBool endOfFlags = PR_FALSE;
    fNextToken++;// eat the first "("
    do {
      if (!PL_strncasecmp(fNextToken, "\\Marked", 7))
        boxSpec->mBoxFlags |= kMarked;
      else if (!PL_strncasecmp(fNextToken, "\\Unmarked", 9))
        boxSpec->mBoxFlags |= kUnmarked;
      else if (!PL_strncasecmp(fNextToken, "\\Noinferiors", 12))
        boxSpec->mBoxFlags |= kNoinferiors;
      else if (!PL_strncasecmp(fNextToken, "\\Noselect", 9))
        boxSpec->mBoxFlags |= kNoselect;
      else if (!PL_strncasecmp(fNextToken, "\\Drafts", 7))
        boxSpec->mBoxFlags |= kImapDrafts;
      else if (!PL_strncasecmp(fNextToken, "\\Trash", 6))
        boxSpec->mBoxFlags |= kImapXListTrash;
      else if (!PL_strncasecmp(fNextToken, "\\Sent", 5))
        boxSpec->mBoxFlags |= kImapSent;
      else if (!PL_strncasecmp(fNextToken, "\\Spam", 5))
        boxSpec->mBoxFlags |= kImapSpam;
      else if (!PL_strncasecmp(fNextToken, "\\AllMail", 8))
        boxSpec->mBoxFlags |= kImapAllMail;
      else if (!PL_strncasecmp(fNextToken, "\\Inbox", 6))
        boxSpec->mBoxFlags |= kImapInbox;
      // we ignore flag other extensions
      
      endOfFlags = *(fNextToken + strlen(fNextToken) - 1) == ')';
      AdvanceToNextToken();
    } while (!endOfFlags && ContinueParse());
    
    if (ContinueParse())
    {
      if (*fNextToken == '"')
      {
        fNextToken++;
        if (*fNextToken == '\\') // handle escaped char
          boxSpec->mHierarchySeparator = *(fNextToken + 1);
        else
          boxSpec->mHierarchySeparator = *fNextToken;
      }
      else	// likely NIL.  Discovered late in 4.02 that we do not handle literals here (e.g. {10} <10 chars>), although this is almost impossibly unlikely
        boxSpec->mHierarchySeparator = kOnlineHierarchySeparatorNil;
      AdvanceToNextToken();
      if (ContinueParse())
      {
        // nsImapProtocol::DiscoverMailboxSpec() eventually frees the
        // boxSpec
        needsToFreeBoxSpec = PR_FALSE;
        mailbox(boxSpec);
      }
    }
  }
  if (needsToFreeBoxSpec)
    NS_RELEASE(boxSpec);
}

/* mailbox         ::= "INBOX" / astring
*/
void nsImapServerResponseParser::mailbox(nsImapMailboxSpec *boxSpec)
{
  char *boxname = nsnull;
  const char *serverKey = fServerConnection.GetImapServerKey();
  PRBool xlistInbox = boxSpec->mBoxFlags & kImapInbox;

  if (!PL_strcasecmp(fNextToken, "INBOX") || xlistInbox)
  {
    boxname = PL_strdup("INBOX");
    if (xlistInbox)
      PR_Free(CreateAstring());
    AdvanceToNextToken();
  }
  else 
  {
    boxname = CreateAstring();
    AdvanceToNextToken();
  }
  
  if (boxname && fHostSessionList)
  {
    // should the namespace check go before or after the Utf7 conversion?
    fHostSessionList->SetNamespaceHierarchyDelimiterFromMailboxForHost(
      serverKey, boxname, boxSpec->mHierarchySeparator);
    
    
    nsIMAPNamespace *ns = nsnull;
    fHostSessionList->GetNamespaceForMailboxForHost(serverKey, boxname, ns);
    if (ns)
    {
      switch (ns->GetType())
      {
      case kPersonalNamespace:
        boxSpec->mBoxFlags |= kPersonalMailbox;
        break;
      case kPublicNamespace:
        boxSpec->mBoxFlags |= kPublicMailbox;
        break;
      case kOtherUsersNamespace:
        boxSpec->mBoxFlags |= kOtherUsersMailbox;
        break;
      default:	// (kUnknownNamespace)
        break;
      }
      boxSpec->mNamespaceForFolder = ns;
    }
    
    //    	char *convertedName =
    //            fServerConnection.CreateUtf7ConvertedString(boxname, PR_FALSE);
    //		PRUnichar *unicharName;
    //        unicharName = fServerConnection.CreatePRUnicharStringFromUTF7(boxname);
    //    	PL_strfree(boxname);
    //    	boxname = convertedName;
  }
  
  if (!boxname)
  {
    if (!fServerConnection.DeathSignalReceived())
      HandleMemoryFailure();
  }
  else if (boxSpec->mConnection && boxSpec->mConnection->GetCurrentUrl())
  {
    boxSpec->mConnection->GetCurrentUrl()->AllocateCanonicalPath(boxname, boxSpec->mHierarchySeparator,
                                                                 getter_Copies(boxSpec->mAllocatedPathName));
    nsIURI *aURL = nsnull;
    boxSpec->mConnection->GetCurrentUrl()->QueryInterface(NS_GET_IID(nsIURI), (void **) &aURL);
    if (aURL)
      aURL->GetHost(boxSpec->mHostName);

    NS_IF_RELEASE(aURL);
    if (boxname)
      PL_strfree( boxname);
    // storage for the boxSpec is now owned by server connection
    fServerConnection.DiscoverMailboxSpec(boxSpec);

    // if this was cancelled by the user,then we sure don't want to
    // send more mailboxes their way
    if (NS_FAILED(fServerConnection.GetConnectionStatus()))
      SetConnected(PR_FALSE);
  }
}


/*
 message_data    ::= nz_number SPACE ("EXPUNGE" /
                              ("FETCH" SPACE msg_fetch) / msg_obsolete)

was changed to

numeric_mailbox_data ::=  number SPACE "EXISTS" / number SPACE "RECENT"
 / nz_number SPACE ("EXPUNGE" /
                              ("FETCH" SPACE msg_fetch) / msg_obsolete)

*/
void nsImapServerResponseParser::numeric_mailbox_data()
{
  PRInt32 tokenNumber = atoi(fNextToken);
  AdvanceToNextToken();
  
  if (ContinueParse())
  {
    if (!PL_strcasecmp(fNextToken, "FETCH"))
    {
      fFetchResponseIndex = tokenNumber;
      AdvanceToNextToken();
      if (ContinueParse())
        msg_fetch(); 
    }
    else if (!PL_strcasecmp(fNextToken, "EXISTS"))
    {
      fNumberOfExistingMessages = tokenNumber;
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken, "RECENT"))
    {
      fNumberOfRecentMessages = tokenNumber;
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken, "EXPUNGE"))
    {
      if (!fServerConnection.GetIgnoreExpunges())
        fFlagState->ExpungeByIndex((PRUint32) tokenNumber);
      skip_to_CRLF();
    }
    else
      msg_obsolete();
  }
}

/*
msg_fetch       ::= "(" 1#("BODY" SPACE body /
"BODYSTRUCTURE" SPACE body /
"BODY[" section "]" SPACE nstring /
"ENVELOPE" SPACE envelope /
"FLAGS" SPACE "(" #(flag / "\Recent") ")" /
"INTERNALDATE" SPACE date_time /
"MODSEQ" SPACE "(" nz_number ")" /
"RFC822" [".HEADER" / ".TEXT"] SPACE nstring /
"RFC822.SIZE" SPACE number /
"UID" SPACE uniqueid) ")"

*/

void nsImapServerResponseParser::msg_fetch()
{
  nsresult res;
  PRBool bNeedEndMessageDownload = PR_FALSE;
  
  // we have not seen a uid response or flags for this fetch, yet
  fCurrentResponseUID = 0;
  fCurrentLineContainedFlagInfo = PR_FALSE;
  fSizeOfMostRecentMessage = 0;  
  // show any incremental progress, for instance, for header downloading
  fServerConnection.ShowProgress();
  
  fNextToken++;	// eat the '(' character
  
  // some of these productions are ignored for now
  while (ContinueParse() && (*fNextToken != ')') )
  {
    if (!PL_strcasecmp(fNextToken, "FLAGS"))
    {
      if (fCurrentResponseUID == 0)
        res = fFlagState->GetUidOfMessage(fFetchResponseIndex - 1, &fCurrentResponseUID);
      
      AdvanceToNextToken();
      if (ContinueParse())
        flags();
      
      if (ContinueParse())
      {	// eat the closing ')'
        fNextToken++;
        // there may be another ')' to close out
        // msg_fetch.  If there is then don't advance
        if (*fNextToken != ')')
          AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken, "UID"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fCurrentResponseUID = atoi(fNextToken);
        if (fCurrentResponseUID > fHighestRecordedUID)
          fHighestRecordedUID = fCurrentResponseUID;
        // size came before UID
        if (fSizeOfMostRecentMessage)
          fReceivedHeaderOrSizeForUID = CurrentResponseUID();
        // if this token ends in ')', then it is the last token
        // else we advance
        char lastTokenChar = *(fNextToken + strlen(fNextToken) - 1);
        if (lastTokenChar == ')')
          fNextToken += strlen(fNextToken) - 1;
        else if (lastTokenChar < '0' || lastTokenChar > '9')
        {
          // GIANT HACK
          // this is a corrupt uid - see if it's pre 5.08 Zimbra omitting
          // a space between the UID and MODSEQ
          if (strlen(fNextToken) > 6 && 
              !strcmp("MODSEQ", fNextToken + strlen(fNextToken) - 6))
            fNextToken += strlen(fNextToken) - 6;
        }
        else
          AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken, "MODSEQ"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fNextToken++; // eat '('
        PRUint64 modSeq =  ParseUint64Str(fNextToken);
        if (modSeq > fHighestModSeq)
          fHighestModSeq = modSeq;

        if (PL_strcasestr(fNextToken, ")"))
        {
          // eat token chars until we get the ')'
          fNextToken = strchr(fNextToken, ')');
          if (fNextToken)
          {
            fNextToken++;
            if (*fNextToken != ')')
              AdvanceToNextToken();
          }
          else
            SetSyntaxError(PR_TRUE);
        }
        else
        {
          SetSyntaxError(PR_TRUE);
        }
      }
    }
    else if (!PL_strcasecmp(fNextToken, "RFC822") ||
      !PL_strcasecmp(fNextToken, "RFC822.HEADER") ||
      !PL_strncasecmp(fNextToken, "BODY[HEADER",11) ||
      !PL_strncasecmp(fNextToken, "BODY[]", 6) ||
      !PL_strcasecmp(fNextToken, "RFC822.TEXT") ||
      (!PL_strncasecmp(fNextToken, "BODY[", 5) &&
				  PL_strstr(fNextToken, "HEADER"))
                                  )
    {
      if (fCurrentResponseUID == 0)
        fFlagState->GetUidOfMessage(fFetchResponseIndex - 1, &fCurrentResponseUID);
      
      if (!PL_strcasecmp(fNextToken, "RFC822.HEADER") ||
        !PL_strcasecmp(fNextToken, "BODY[HEADER]"))
      {
        // all of this message's headers
        AdvanceToNextToken();
        fDownloadingHeaders = PR_TRUE;
        BeginMessageDownload(MESSAGE_RFC822); // initialize header parser
        bNeedEndMessageDownload = PR_FALSE;
        if (ContinueParse())
          msg_fetch_headers(nsnull);
      }
      else if (!PL_strncasecmp(fNextToken, "BODY[HEADER.FIELDS",19))
      {
        fDownloadingHeaders = PR_TRUE;
        BeginMessageDownload(MESSAGE_RFC822); // initialize header parser
        // specific message header fields
        while (ContinueParse() && fNextToken[strlen(fNextToken)-1] != ']')
          AdvanceToNextToken();
        if (ContinueParse())
        {
          bNeedEndMessageDownload = PR_FALSE;
          AdvanceToNextToken();
          if (ContinueParse())
            msg_fetch_headers(nsnull);
        }
      }
      else
      {
        char *whereHeader = PL_strstr(fNextToken, "HEADER");
        if (whereHeader)
        {
          const char *startPartNum = fNextToken + 5;
          if (whereHeader > startPartNum)
          {
            PRInt32 partLength = whereHeader - startPartNum - 1; //-1 for the dot!
            char *partNum = (char *)PR_CALLOC((partLength + 1) * sizeof (char));
            if (partNum)
            {
              PL_strncpy(partNum, startPartNum, partLength);
              if (ContinueParse())
              {
                if (PL_strstr(fNextToken, "FIELDS"))
                {
                  while (ContinueParse() && fNextToken[strlen(fNextToken)-1] != ']')
                    AdvanceToNextToken();
                }
                if (ContinueParse())
                {
                  AdvanceToNextToken();
                  if (ContinueParse())
                    msg_fetch_headers(partNum);
                }
              }
              PR_Free(partNum);
            }
          }
          else
            SetSyntaxError(PR_TRUE);
        }
        else
        {
          fDownloadingHeaders = PR_FALSE;
          
          PRBool chunk = PR_FALSE;
          PRInt32 origin = 0;
          if (!PL_strncasecmp(fNextToken, "BODY[]<", 7))
          {
            char *tokenCopy = 0;
            tokenCopy = PL_strdup(fNextToken);
            if (tokenCopy)
            {
              char *originString = tokenCopy + 7;	// where the byte number starts
              char *closeBracket = PL_strchr(tokenCopy,'>');
              if (closeBracket && originString && *originString)
              {
                *closeBracket = 0;
                origin = atoi(originString);
                chunk = PR_TRUE;
              }
              PR_Free(tokenCopy);
            }
          }
          
          AdvanceToNextToken();
          if (ContinueParse())
          {
            msg_fetch_content(chunk, origin, MESSAGE_RFC822);
          }
        }
      }
      }
      else if (!PL_strcasecmp(fNextToken, "RFC822.SIZE") || !PL_strcasecmp(fNextToken, "XAOL.SIZE"))
      {
        AdvanceToNextToken();
        if (ContinueParse())
        {
          PRBool sendEndMsgDownload = (GetDownloadingHeaders() 
                                        && fReceivedHeaderOrSizeForUID == CurrentResponseUID());
          fSizeOfMostRecentMessage = atoi(fNextToken);
          fReceivedHeaderOrSizeForUID = CurrentResponseUID();
          if (sendEndMsgDownload)
          {
            fServerConnection.NormalMessageEndDownload();
            fReceivedHeaderOrSizeForUID = nsMsgKey_None;
          }

          // if we are in the process of fetching everything RFC822 then we should
          // turn around and force the total download size to be set to this value.
          // this helps if the server gaves us a bogus size for the message in response to the 
          // envelope command.
          if (fFetchEverythingRFC822)
            SetTotalDownloadSize(fSizeOfMostRecentMessage);
          
          if (fSizeOfMostRecentMessage == 0 && CurrentResponseUID())
          {
            // on no, bogus Netscape 2.0 mail server bug
            char uidString[100];
            sprintf(uidString, "%ld", (long)CurrentResponseUID());
            
            if (!fZeroLengthMessageUidString.IsEmpty())
              fZeroLengthMessageUidString += ",";
            
            fZeroLengthMessageUidString += uidString;
          }
          
          // if this token ends in ')', then it is the last token
          // else we advance
          if ( *(fNextToken + strlen(fNextToken) - 1) == ')')
            fNextToken += strlen(fNextToken) - 1;
          else
            AdvanceToNextToken();
        }
      }
      else if (!PL_strcasecmp(fNextToken, "XSENDER"))
      {
        PR_FREEIF(fXSenderInfo);
        AdvanceToNextToken();
        if (! fNextToken) 
          SetSyntaxError(PR_TRUE);
        else
        {
          fXSenderInfo = CreateAstring(); 
          AdvanceToNextToken();
        }
      }
      // I only fetch RFC822 so I should never see these BODY responses
      else if (!PL_strcasecmp(fNextToken, "BODY"))
        skip_to_CRLF(); // I never ask for this
      else if (!PL_strcasecmp(fNextToken, "BODYSTRUCTURE"))
      {
        if (fCurrentResponseUID == 0)
          fFlagState->GetUidOfMessage(fFetchResponseIndex - 1, &fCurrentResponseUID);
        bodystructure_data();
      }
      else if (!PL_strncasecmp(fNextToken, "BODY[TEXT", 9))
      {
        mime_data();
      }
      else if (!PL_strncasecmp(fNextToken, "BODY[", 5) && PL_strncasecmp(fNextToken, "BODY[]", 6))
      {
        fDownloadingHeaders = PR_FALSE;
        // A specific MIME part, or MIME part header
        mime_data();
      }
      else if (!PL_strcasecmp(fNextToken, "ENVELOPE"))
      {
        fDownloadingHeaders = PR_TRUE;
        bNeedEndMessageDownload = PR_TRUE;
        BeginMessageDownload(MESSAGE_RFC822);
        envelope_data(); 
      }
      else if (!PL_strcasecmp(fNextToken, "INTERNALDATE"))
      {
        fDownloadingHeaders = PR_TRUE; // we only request internal date while downloading headers
        if (!bNeedEndMessageDownload)
          BeginMessageDownload(MESSAGE_RFC822);
        bNeedEndMessageDownload = PR_TRUE;
        internal_date(); 
      }
      else if (!PL_strcasecmp(fNextToken, "XAOL-ENVELOPE"))
      {
        fDownloadingHeaders = PR_TRUE;
        if (!bNeedEndMessageDownload)
          BeginMessageDownload(MESSAGE_RFC822);
        bNeedEndMessageDownload = PR_TRUE;
        xaolenvelope_data();
      }
      else
      {
        nsImapAction imapAction; 
        if (!fServerConnection.GetCurrentUrl())
          return;
        fServerConnection.GetCurrentUrl()->GetImapAction(&imapAction);
        nsCAutoString userDefinedFetchAttribute;
        fServerConnection.GetCurrentUrl()->GetCustomAttributeToFetch(userDefinedFetchAttribute);
        if (imapAction == nsIImapUrl::nsImapUserDefinedFetchAttribute && !strcmp(userDefinedFetchAttribute.get(), fNextToken))
        {
          AdvanceToNextToken();
          char *fetchResult = CreateParenGroup();
          // look through the tokens until we find the closing ')'
          // we can have a result like the following:
          // ((A B) (C D) (E F))
          fServerConnection.GetCurrentUrl()->SetCustomAttributeResult(nsDependentCString(fetchResult));
          PR_Free(fetchResult);
          break;
        }
        else
          SetSyntaxError(PR_TRUE);
      }
      
        }
        
        if (ContinueParse())
        {
          if (CurrentResponseUID() && CurrentResponseUID() != nsMsgKey_None 
            && fCurrentLineContainedFlagInfo && fFlagState)
          {
            fFlagState->AddUidFlagPair(CurrentResponseUID(), fSavedFlagInfo, fFetchResponseIndex - 1);
            for (PRInt32 i = 0; i < fCustomFlags.Count(); i++)
              fFlagState->AddUidCustomFlagPair(CurrentResponseUID(), fCustomFlags.CStringAt(i)->get());
            fCustomFlags.Clear();
          }
          
          if (fFetchingAllFlags)
            fCurrentLineContainedFlagInfo = PR_FALSE;	// do not fire if in PostProcessEndOfLine          
          
          AdvanceToNextToken();	// eat the ')' ending token
										// should be at end of line
          if (bNeedEndMessageDownload)
          {
            if (ContinueParse())
            {
              // complete the message download
              fServerConnection.NormalMessageEndDownload();
            }
            else
              fServerConnection.AbortMessageDownLoad();
          }
          
        }
}

typedef enum _envelopeItemType
{
	envelopeString,
	envelopeAddress
} envelopeItemType;

typedef struct 
{
	const char * name;
	envelopeItemType type;
} envelopeItem;

// RFC3501:  envelope  = "(" env-date SP env-subject SP env-from SP
//                       env-sender SP env-reply-to SP env-to SP env-cc SP
//                       env-bcc SP env-in-reply-to SP env-message-id ")"
//           env-date    = nstring
//           env-subject = nstring
//           env-from    = "(" 1*address ")" / nil
//           env-sender  = "(" 1*address ")" / nil
//           env-reply-to= "(" 1*address ")" / nil
//           env-to      = "(" 1*address ")" / nil
//           env-cc      = "(" 1*address ")" / nil
//           env-bcc     = "(" 1*address ")" / nil
//           env-in-reply-to = nstring
//           env-message-id  = nstring

static const envelopeItem EnvelopeTable[] =
{
  {"Date", envelopeString},
  {"Subject", envelopeString},
  {"From", envelopeAddress},
  {"Sender", envelopeAddress},
  {"Reply-to", envelopeAddress},
  {"To", envelopeAddress},
  {"Cc", envelopeAddress},
  {"Bcc", envelopeAddress},
  {"In-reply-to", envelopeString},
  {"Message-id", envelopeString}
};

void nsImapServerResponseParser::envelope_data()
{
  AdvanceToNextToken();
  fNextToken++; // eat '('
  for (int tableIndex = 0; tableIndex < (int)(sizeof(EnvelopeTable) / sizeof(EnvelopeTable[0])); tableIndex++)
  {
    if (!ContinueParse())
      break;
    else if (*fNextToken == ')')
    {
      SetSyntaxError(PR_TRUE); // envelope too short
      break;
    }
    else
    {
      nsCAutoString headerLine(EnvelopeTable[tableIndex].name);
      headerLine += ": ";
      PRBool headerNonNil = PR_TRUE;
      if (EnvelopeTable[tableIndex].type == envelopeString)
      {
        nsCAutoString strValue;
        strValue.Adopt(CreateNilString());
        if (!strValue.IsEmpty())
          headerLine.Append(strValue);
        else
          headerNonNil = PR_FALSE;
      }
      else
      {
        nsCAutoString address;
        parse_address(address);
        headerLine += address;
        if (address.IsEmpty())
          headerNonNil = PR_FALSE;
      }
      if (headerNonNil)
        fServerConnection.HandleMessageDownLoadLine(headerLine.get(), PR_FALSE);
    }
    if (ContinueParse())
      AdvanceToNextToken();
  }
  // Now we should be at the end of the envelope and have *fToken == ')'.
  // Skip this last parenthesis.
  AdvanceToNextToken();
}

void nsImapServerResponseParser::xaolenvelope_data()
{
  // eat the opening '('
  fNextToken++;
  
  if (ContinueParse() && (*fNextToken != ')'))
  {
    AdvanceToNextToken();
    fNextToken++; // eat '('
    nsCAutoString subject;
    subject.Adopt(CreateNilString());
    nsCAutoString subjectLine("Subject: ");
    subjectLine += subject;
    fServerConnection.HandleMessageDownLoadLine(subjectLine.get(), PR_FALSE);
    fNextToken++; // eat the next '('
    if (ContinueParse())
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        nsCAutoString fromLine;
        if (!strcmp(GetSelectedMailboxName(), "Sent Items"))
        {
          // xaol envelope switches the From with the To, so we switch them back and
          // create a fake from line From: user@aol.com
          fromLine.Append("To: ");
          nsCAutoString fakeFromLine(NS_LITERAL_CSTRING("From: ") + nsDependentCString(fServerConnection.GetImapUserName()) + NS_LITERAL_CSTRING("@aol.com"));
          fServerConnection.HandleMessageDownLoadLine(fakeFromLine.get(), PR_FALSE);
        }
        else
        {
          fromLine.Append("From: ");
        }
        parse_address(fromLine);
        fServerConnection.HandleMessageDownLoadLine(fromLine.get(), PR_FALSE);
        if (ContinueParse())
        {
          AdvanceToNextToken();	// ge attachment size
          PRInt32 attachmentSize = atoi(fNextToken);
          if (attachmentSize != 0)
          {
            nsCAutoString attachmentLine("X-attachment-size: ");
            attachmentLine.AppendInt(attachmentSize);
            fServerConnection.HandleMessageDownLoadLine(attachmentLine.get(), PR_FALSE);
          }
        }
        if (ContinueParse())
        {
          AdvanceToNextToken();	// skip image size
          PRInt32 imageSize = atoi(fNextToken);
          if (imageSize != 0)
          {
            nsCAutoString imageLine("X-image-size: ");
            imageLine.AppendInt(imageSize);
            fServerConnection.HandleMessageDownLoadLine(imageLine.get(), PR_FALSE);
          }
        }
        if (ContinueParse())
          AdvanceToNextToken();	// skip )
      }
    }
  }
}

void nsImapServerResponseParser::parse_address(nsCAutoString &addressLine)
{
  if (!strcmp(fNextToken, "NIL"))
    return;
  PRBool firstAddress = PR_TRUE;
  // should really look at chars here
  NS_ASSERTION(*fNextToken == '(', "address should start with '('");
  fNextToken++; // eat the next '('
  while (ContinueParse() && *fNextToken == '(')
  {
    NS_ASSERTION(*fNextToken == '(', "address should start with '('");
    fNextToken++; // eat the next '('
    
    if (!firstAddress)
      addressLine += ", ";
    
    firstAddress = PR_FALSE;
    char *personalName = CreateNilString();
    AdvanceToNextToken();
    char *atDomainList = CreateNilString();
    if (ContinueParse())
    {
      AdvanceToNextToken();
      char *mailboxName = CreateNilString();
      if (ContinueParse())
      {
        AdvanceToNextToken();
        char *hostName = CreateNilString();
        AdvanceToNextToken();
        addressLine += mailboxName;
        if (hostName)
        {
          addressLine += '@';
          addressLine += hostName;
          NS_Free(hostName);
        }
        if (personalName)
        {
          addressLine += " (";
          addressLine += personalName;
          addressLine += ')';
        }
      }
    }
    PR_Free(personalName);
    PR_Free(atDomainList);
    
    if (*fNextToken == ')')
      fNextToken++;
    // if the next token isn't a ')' for the address term,
    // then we must have another address pair left....so get the next
    // token and continue parsing in this loop...
    if ( *fNextToken == '\0' )
      AdvanceToNextToken();
    
  }
  if (*fNextToken == ')')
    fNextToken++;
  //	AdvanceToNextToken();	// skip "))"
}

void nsImapServerResponseParser::internal_date()
{
  AdvanceToNextToken();
  if (ContinueParse())
  {
    nsCAutoString dateLine("Date: ");
    char *strValue = CreateNilString();
    if (strValue)
    {
      dateLine += strValue;
      NS_Free(strValue);
    }
    fServerConnection.HandleMessageDownLoadLine(dateLine.get(), PR_FALSE);
  }
  // advance the parser.
  AdvanceToNextToken();
}

void nsImapServerResponseParser::flags()
{
  imapMessageFlagsType messageFlags = kNoImapMsgFlag;
  fCustomFlags.Clear();

  // clear the custom flags for this message
  // otherwise the old custom flags will stay around
  // see bug #191042
  if (fFlagState && CurrentResponseUID() != nsMsgKey_None)
    fFlagState->ClearCustomFlags(CurrentResponseUID());

  // eat the opening '('
  fNextToken++;
  while (ContinueParse() && (*fNextToken != ')'))
  {
    PRBool knownFlag = PR_FALSE;					     
    if (*fNextToken == '\\')
    {
      switch (toupper(fNextToken[1])) {
      case 'S':
        if (!PL_strncasecmp(fNextToken, "\\Seen",5))
        {
          messageFlags |= kImapMsgSeenFlag;
          knownFlag = PR_TRUE;
        }
        break;
      case 'A':
        if (!PL_strncasecmp(fNextToken, "\\Answered",9))
        {
          messageFlags |= kImapMsgAnsweredFlag;
          knownFlag = PR_TRUE;
        }
        break;
      case 'F':
        if (!PL_strncasecmp(fNextToken, "\\Flagged",8))
        {
          messageFlags |= kImapMsgFlaggedFlag;
          knownFlag = PR_TRUE;
        }
        break;
      case 'D':
        if (!PL_strncasecmp(fNextToken, "\\Deleted",8))
        {
          messageFlags |= kImapMsgDeletedFlag;
          knownFlag = PR_TRUE;
        }
        else if (!PL_strncasecmp(fNextToken, "\\Draft",6))
        {
          messageFlags |= kImapMsgDraftFlag;
          knownFlag = PR_TRUE;
        }
        break;
      case 'R':
        if (!PL_strncasecmp(fNextToken, "\\Recent",7))
        {
          messageFlags |= kImapMsgRecentFlag;
          knownFlag = PR_TRUE;
        }
        break;
      default:
        break;
      }
    }
    else if (*fNextToken == '$')
    {
      switch (toupper(fNextToken[1])) {
      case 'M':
        if ((fSupportsUserDefinedFlags & (kImapMsgSupportUserFlag |
          kImapMsgSupportMDNSentFlag))
          && !PL_strncasecmp(fNextToken, "$MDNSent",8))
        {
          messageFlags |= kImapMsgMDNSentFlag;
          knownFlag = PR_TRUE;
        }
        break;
      case 'F':
        if ((fSupportsUserDefinedFlags & (kImapMsgSupportUserFlag |
          kImapMsgSupportForwardedFlag))
          && !PL_strncasecmp(fNextToken, "$Forwarded",10))
        {
          messageFlags |= kImapMsgForwardedFlag;
          knownFlag = PR_TRUE;
        }
        break;
      default:
        break;
      }
    }
    if (!knownFlag && fFlagState)
    {
      nsCAutoString flag(fNextToken);
      PRInt32 parenIndex = flag.FindChar(')');
      if (parenIndex > 0)
        flag.Truncate(parenIndex);
      messageFlags |= kImapMsgCustomKeywordFlag;
      if (CurrentResponseUID() != nsMsgKey_None)
        fFlagState->AddUidCustomFlagPair(CurrentResponseUID(), flag.get());
      else
        fCustomFlags.AppendCString(flag);
    }
    if (PL_strcasestr(fNextToken, ")"))
    {
      // eat token chars until we get the ')'
      while (*fNextToken != ')')
        fNextToken++;
    }
    else
      AdvanceToNextToken();
  }
  
  if (ContinueParse())
    while(*fNextToken != ')')
      fNextToken++;
    
    fCurrentLineContainedFlagInfo = PR_TRUE;	// handled in PostProcessEndOfLine
    fSavedFlagInfo = messageFlags;
}

// RFC3501:  resp-cond-state = ("OK" / "NO" / "BAD") SP resp-text
//                             ; Status condition
void nsImapServerResponseParser::resp_cond_state(PRBool isTagged)
{
  // According to RFC3501, Sec. 7.1, the untagged NO response "indicates a
  // warning; the command can still complete successfully."
  // However, the untagged BAD response "indicates a protocol-level error for
  // which the associated command can not be determined; it can also indicate an
  // internal server failure." 
  // Thus, we flag an error for a tagged NO response and for any BAD response.
  if (isTagged && !PL_strcasecmp(fNextToken, "NO") ||
    !PL_strcasecmp(fNextToken, "BAD"))
    fCurrentCommandFailed = PR_TRUE;
  
  AdvanceToNextToken();
  if (ContinueParse())
    resp_text();
}

/*
resp_text       ::= ["[" resp_text_code "]" SPACE] (text_mime2 / text)

  was changed to in order to enable a one symbol look ahead predictive
  parser.
  
    resp_text       ::= ["[" resp_text_code  SPACE] (text_mime2 / text)
*/
void nsImapServerResponseParser::resp_text()
{
  if (ContinueParse() && (*fNextToken == '['))
    resp_text_code();
		
  if (ContinueParse())
  {
    if (!PL_strcmp(fNextToken, "=?"))
      text_mime2();
    else
      text();
  }
}
/*
 text_mime2       ::= "=?" <charset> "?" <encoding> "?"
                               <encoded-text> "?="
                               ;; Syntax defined in [MIME-2]
*/
void nsImapServerResponseParser::text_mime2()
{
  skip_to_CRLF();
}

/*
 text            ::= 1*TEXT_CHAR

*/
void nsImapServerResponseParser::text()
{
  skip_to_CRLF();
}

void nsImapServerResponseParser::parse_folder_flags()
{
  PRUint16 labelFlags = 0;

  do 
  {
    AdvanceToNextToken();
    if (*fNextToken == '(')
      fNextToken++;
    if (!PL_strncasecmp(fNextToken, "$MDNSent", 8))
      fSupportsUserDefinedFlags |= kImapMsgSupportMDNSentFlag;
    else if (!PL_strncasecmp(fNextToken, "$Forwarded", 10))
      fSupportsUserDefinedFlags |= kImapMsgSupportForwardedFlag;
    else if (!PL_strncasecmp(fNextToken, "\\Seen", 5))
      fSettablePermanentFlags |= kImapMsgSeenFlag;
    else if (!PL_strncasecmp(fNextToken, "\\Answered", 9))
      fSettablePermanentFlags |= kImapMsgAnsweredFlag;
    else if (!PL_strncasecmp(fNextToken, "\\Flagged", 8))
      fSettablePermanentFlags |= kImapMsgFlaggedFlag;
    else if (!PL_strncasecmp(fNextToken, "\\Deleted", 8))
      fSettablePermanentFlags |= kImapMsgDeletedFlag;
    else if (!PL_strncasecmp(fNextToken, "\\Draft", 6))
      fSettablePermanentFlags |= kImapMsgDraftFlag;
    else if (!PL_strncasecmp(fNextToken, "$Label1", 7))
      labelFlags |= 1;
    else if (!PL_strncasecmp(fNextToken, "$Label2", 7))
      labelFlags |= 2;
    else if (!PL_strncasecmp(fNextToken, "$Label3", 7))
      labelFlags |= 4;
    else if (!PL_strncasecmp(fNextToken, "$Label4", 7))
      labelFlags |= 8;
    else if (!PL_strncasecmp(fNextToken, "$Label5", 7))
      labelFlags |= 16;
    else if (!PL_strncasecmp(fNextToken, "\\*", 2))
    {
      fSupportsUserDefinedFlags |= kImapMsgSupportUserFlag;
      fSupportsUserDefinedFlags |= kImapMsgSupportForwardedFlag;
      fSupportsUserDefinedFlags |= kImapMsgSupportMDNSentFlag;
      fSupportsUserDefinedFlags |= kImapMsgLabelFlags;
    }
  } while (!fAtEndOfLine && ContinueParse());

  if (labelFlags == 31)
    fSupportsUserDefinedFlags |= kImapMsgLabelFlags;

  if (fFlagState)
    fFlagState->OrSupportedUserFlags(fSupportsUserDefinedFlags);
}
/*
  resp_text_code  ::= ("ALERT" / "PARSE" /
                              "PERMANENTFLAGS" SPACE "(" #(flag / "\*") ")" /
                              "READ-ONLY" / "READ-WRITE" / "TRYCREATE" /
                              "UIDVALIDITY" SPACE nz_number /
                              "UNSEEN" SPACE nz_number /
                              "HIGHESTMODSEQ" SPACE nz_number /
                              "NOMODSEQ" /
                              atom [SPACE 1*<any TEXT_CHAR except "]">] )
                      "]"

 
*/
void nsImapServerResponseParser::resp_text_code()
{
  // this is a special case way of advancing the token
  // strtok won't break up "[ALERT]" into separate tokens
  if (strlen(fNextToken) > 1)
    fNextToken++;
  else 
    AdvanceToNextToken();
  
  if (ContinueParse())
  {
    if (!PL_strcasecmp(fNextToken,"ALERT]"))
    {
      char *alertMsg = fCurrentTokenPlaceHolder;	// advance past ALERT]
      if (alertMsg && *alertMsg && (!fLastAlert || PL_strcmp(fNextToken, fLastAlert)))
      {
        fServerConnection.AlertUserEvent(alertMsg);
        PR_Free(fLastAlert);
        fLastAlert = PL_strdup(alertMsg);
      }
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken,"PARSE]"))
    {
      // do nothing for now
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken,"NETSCAPE]"))
    {
      skip_to_CRLF();
    }
    else if (!PL_strcasecmp(fNextToken,"PERMANENTFLAGS"))
    {
      PRUint32 saveSettableFlags = fSettablePermanentFlags;
      fSupportsUserDefinedFlags = 0;		// assume no unless told
      fSettablePermanentFlags = 0;            // assume none, unless told otherwise.
      parse_folder_flags();
      // if the server tells us there are no permanent flags, we're
      // just going to pretend that the FLAGS response flags, if any, are
      // permanent in case the server is broken. This will allow us
      // to store delete and seen flag changes - if they're not permanent, 
      // they're not permanent, but at least we'll try to set them.
      if (!fSettablePermanentFlags)
        fSettablePermanentFlags = saveSettableFlags;
      fGotPermanentFlags = PR_TRUE;
    }
    else if (!PL_strcasecmp(fNextToken,"READ-ONLY]"))
    {
      fCurrentFolderReadOnly = PR_TRUE;
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken,"READ-WRITE]"))
    {
      fCurrentFolderReadOnly = PR_FALSE;
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken,"TRYCREATE]"))
    {
      // do nothing for now
      AdvanceToNextToken();
    }
    else if (!PL_strcasecmp(fNextToken,"UIDVALIDITY"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fFolderUIDValidity = atoi(fNextToken);
        fHighestRecordedUID = 0;
        AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken,"UNSEEN"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fNumberOfUnseenMessages = atoi(fNextToken);
        AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken,"UIDNEXT"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fStatusNextUID = atoi(fNextToken);
        AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken, "APPENDUID"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        // ** jt -- the returned uidvalidity is the destination folder
        // uidvalidity; don't use it for current folder
        // fFolderUIDValidity = atoi(fNextToken);
        // fHighestRecordedUID = 0; ??? this should be wrong
        AdvanceToNextToken();
        if (ContinueParse())
        {
          fCurrentResponseUID = atoi(fNextToken);
          AdvanceToNextToken();
        }
      }
    }
    else if (!PL_strcasecmp(fNextToken, "COPYUID"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        // ** jt -- destination folder uidvalidity
        // fFolderUIDValidity = atoi(fNextToken);
        // original message set; ignore it
        AdvanceToNextToken();
        if (ContinueParse())
        {
          // the resulting message set; should be in the form of
          // either uid or uid1:uid2
          AdvanceToNextToken();
          // clear copy response uid
          fServerConnection.SetCopyResponseUid(fNextToken);
        }
        if (ContinueParse())
          AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken, "HIGHESTMODSEQ"))
    {
      AdvanceToNextToken();
      if (ContinueParse())
      {
        fHighestModSeq = ParseUint64Str(fNextToken); 
        fUseModSeq = PR_TRUE;
        AdvanceToNextToken();
      }
    }
    else if (!PL_strcasecmp(fNextToken, "NOMODSEQ]"))
    {
      fHighestModSeq = 0;
      fUseModSeq = PR_FALSE;
      skip_to_CRLF();
    }
    else if (!PL_strcasecmp(fNextToken, "CAPABILITY"))
    {
      capability_data();
    }
    else if (!PL_strcasecmp(fNextToken, "MYRIGHTS"))
    {
      myrights_data(PR_TRUE);      
    }
    else // just text
    {
      // do nothing but eat tokens until we see the ] or CRLF
      // we should see the ] but we don't want to go into an
      // endless loop if the CRLF is not there
      do 
      {
        AdvanceToNextToken();
      } while (!PL_strcasestr(fNextToken, "]") && !fAtEndOfLine 
                && ContinueParse());
    }
  }
}

// RFC3501:  response-done = response-tagged / response-fatal
 void nsImapServerResponseParser::response_done()
 {
   if (ContinueParse())
   {
     if (!PL_strcmp(fCurrentCommandTag, fNextToken))
       response_tagged();
     else
       response_fatal();
   }
 }
 
// RFC3501:  response-tagged = tag SP resp-cond-state CRLF
 void nsImapServerResponseParser::response_tagged()
 {
   // eat the tag
   AdvanceToNextToken();
   if (ContinueParse())
   {
     resp_cond_state(PR_TRUE);
     if (ContinueParse())
     {
       if (!fAtEndOfLine)
         SetSyntaxError(PR_TRUE);
       else if (!fCurrentCommandFailed)
         ResetLexAnalyzer();
     }
   }
 }
 
// RFC3501:  response-fatal = "*" SP resp-cond-bye CRLF
//                              ; Server closes connection immediately
 void nsImapServerResponseParser::response_fatal()
 {
   // eat the "*"
   AdvanceToNextToken();
   if (ContinueParse())
     resp_cond_bye();
 }

// RFC3501:  resp-cond-bye = "BYE" SP resp-text
void nsImapServerResponseParser::resp_cond_bye()
{
  SetConnected(PR_FALSE);
  fIMAPstate = kNonAuthenticated;
}


void nsImapServerResponseParser::msg_fetch_headers(const char *partNum)
{
  if (GetFillingInShell())
  {
    char *headerData = CreateAstring();
    AdvanceToNextToken();
    m_shell->AdoptMessageHeaders(headerData, partNum);
  }
  else
  {
    msg_fetch_content(PR_FALSE, 0, MESSAGE_RFC822);
  }
}


/* nstring         ::= string / nil
string          ::= quoted / literal
nil             ::= "NIL"

*/
void nsImapServerResponseParser::msg_fetch_content(PRBool chunk, PRInt32 origin, const char *content_type)
{
  // setup the stream for downloading this message.
  // Don't do it if we are filling in a shell or downloading a part.
  // DO do it if we are downloading a whole message as a result of
  // an invalid shell trying to generate.
  if ((!chunk || (origin == 0)) && !GetDownloadingHeaders() &&
    (GetFillingInShell() ? m_shell->GetGeneratingWholeMessage() : PR_TRUE))
  {
    if (NS_FAILED(BeginMessageDownload(content_type)))
      return;
  }
  
  if (PL_strcasecmp(fNextToken, "NIL"))
  {
    if (*fNextToken == '"')
      fLastChunk = msg_fetch_quoted(chunk, origin);
    else
      fLastChunk = msg_fetch_literal(chunk, origin);
  }
  else
    AdvanceToNextToken();	// eat "NIL"
  
  if (fLastChunk && (GetFillingInShell() ? m_shell->GetGeneratingWholeMessage() : PR_TRUE))
  {
    // complete the message download
    if (ContinueParse())
    {
      if (fReceivedHeaderOrSizeForUID == CurrentResponseUID())
      {
        fServerConnection.NormalMessageEndDownload();
        fReceivedHeaderOrSizeForUID = nsMsgKey_None;
      }
      else
         fReceivedHeaderOrSizeForUID = CurrentResponseUID();
    }
    else
      fServerConnection.AbortMessageDownLoad();
  }
}


/*
quoted          ::= <"> *QUOTED_CHAR <">

  QUOTED_CHAR     ::= <any TEXT_CHAR except quoted_specials> /
  "\" quoted_specials
  
    quoted_specials ::= <"> / "\"
*/

PRBool nsImapServerResponseParser::msg_fetch_quoted(PRBool chunk, PRInt32 origin)
{
  
#ifdef DEBUG_chrisf
	 PR_ASSERT(!chunk);
#endif
         
         char *q = CreateQuoted();
         if (q)
         {
           fServerConnection.HandleMessageDownLoadLine(q, PR_FALSE, q);
           PR_Free(q);
         }
         
         AdvanceToNextToken();
         
         PRBool lastChunk = !chunk || ((origin + numberOfCharsInThisChunk) >= fTotalDownloadSize);
         return lastChunk;
}
/* msg_obsolete    ::= "COPY" / ("STORE" SPACE msg_fetch)
;; OBSOLETE untagged data responses */
void nsImapServerResponseParser::msg_obsolete()
{
  if (!PL_strcasecmp(fNextToken, "COPY"))
    AdvanceToNextToken();
  else if (!PL_strcasecmp(fNextToken, "STORE"))
  {
    AdvanceToNextToken();
    if (ContinueParse())
      msg_fetch();
  }
  else
    SetSyntaxError(PR_TRUE);
  
}

void nsImapServerResponseParser::capability_data()
{
  PRInt32 endToken = -1;
  fCapabilityFlag = kCapabilityDefined | kHasAuthOldLoginCapability;
  do {
    AdvanceToNextToken();
    if (fNextToken) {
      nsCString token(fNextToken);
      endToken = token.FindChar(']');
      if (endToken >= 0)
        token.Truncate(endToken);

      if(token.Equals("AUTH=LOGIN", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasAuthLoginCapability;
      else if(token.Equals("AUTH=PLAIN", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasAuthPlainCapability;
      else if (token.Equals("AUTH=CRAM-MD5", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasCRAMCapability;
      else if (token.Equals("AUTH=NTLM", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasAuthNTLMCapability;
      else if (token.Equals("AUTH=GSSAPI", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasAuthGssApiCapability;
      else if (token.Equals("AUTH=MSN", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasAuthMSNCapability;
      else if (token.Equals("STARTTLS", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasStartTLSCapability;
      else if (token.Equals("LOGINDISABLED", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag &= ~kHasAuthOldLoginCapability; // remove flag
      else if (token.Equals("XSENDER", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasXSenderCapability;
      else if (token.Equals("IMAP4", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kIMAP4Capability;
      else if (token.Equals("IMAP4rev1", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kIMAP4rev1Capability;
      else if (Substring(token,0,5).Equals("IMAP4", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kIMAP4other;
      else if (token.Equals("X-NO-ATOMIC-RENAME", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kNoHierarchyRename;
      else if (token.Equals("X-NON-HIERARCHICAL-RENAME", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kNoHierarchyRename;
      else if (token.Equals("NAMESPACE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kNamespaceCapability;
      else if (token.Equals("MAILBOXDATA", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kMailboxDataCapability;
      else if (token.Equals("ACL", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kACLCapability;
      else if (token.Equals("XSERVERINFO", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kXServerInfoCapability;
      else if (token.Equals("UIDPLUS", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kUidplusCapability;
      else if (token.Equals("LITERAL+", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kLiteralPlusCapability;
      else if (token.Equals("XAOL-OPTION", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kAOLImapCapability;
      else if (token.Equals("QUOTA", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kQuotaCapability;
      else if (token.Equals("LANGUAGE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasLanguageCapability;
      else if (token.Equals("IDLE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasIdleCapability;
      else if (token.Equals("CONDSTORE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasCondStoreCapability;
      else if (token.Equals("ENABLE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasEnableCapability;
      else if (token.Equals("XLIST", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasXListCapability;
      else if (token.Equals("COMPRESS=DEFLATE", nsCaseInsensitiveCStringComparator()))
        fCapabilityFlag |= kHasCompressDeflateCapability;
    }
  } while (fNextToken && endToken < 0 && !fAtEndOfLine && ContinueParse());

  if (fHostSessionList)
    fHostSessionList->SetCapabilityForHost(
    fServerConnection.GetImapServerKey(), 
    fCapabilityFlag);
  nsImapProtocol *navCon = &fServerConnection;
  NS_ASSERTION(navCon, "null imap protocol connection while parsing capability response");	// we should always have this
  if (navCon)
    navCon->CommitCapability();
  skip_to_CRLF();
}

void nsImapServerResponseParser::xmailboxinfo_data()
{
  AdvanceToNextToken();
  if (!fNextToken)
    return;
  
  char *mailboxName = CreateAstring(); // PL_strdup(fNextToken);
  if (mailboxName)
  {
    do 
    {
      AdvanceToNextToken();
      if (fNextToken) 
      {
        if (!PL_strcmp("MANAGEURL", fNextToken))
        {
          AdvanceToNextToken();
          fFolderAdminUrl = CreateAstring();
        }
        else if (!PL_strcmp("POSTURL", fNextToken))
        {
          AdvanceToNextToken();
          // ignore this for now...
        }
      }
    } while (fNextToken && !fAtEndOfLine && ContinueParse());
  }
}

void nsImapServerResponseParser::xserverinfo_data()
{
  do 
  {
    AdvanceToNextToken();
    if (!fNextToken)
      break;
    if (!PL_strcmp("MANAGEACCOUNTURL", fNextToken))
    {
      AdvanceToNextToken();
      fMailAccountUrl.Adopt(CreateNilString());
    }
    else if (!PL_strcmp("MANAGELISTSURL", fNextToken))
    {
      AdvanceToNextToken();
      fManageListsUrl.Adopt(CreateNilString());
    }
    else if (!PL_strcmp("MANAGEFILTERSURL", fNextToken))
    {
      AdvanceToNextToken();
      fManageFiltersUrl.Adopt(CreateNilString());
    }
  } while (fNextToken && !fAtEndOfLine && ContinueParse());
}

void nsImapServerResponseParser::enable_data()
{
  do
  {
    // eat each enable response;
     AdvanceToNextToken();
     if (!strcmp("CONDSTORE", fNextToken))
       fCondStoreEnabled = PR_TRUE;
  } while (fNextToken && !fAtEndOfLine && ContinueParse());
  
}

void nsImapServerResponseParser::language_data()
{
  // we may want to go out and store the language returned to us
  // by the language command in the host info session stuff. 

  // for now, just eat the language....
  do
  {
    // eat each language returned to us
    AdvanceToNextToken();
  } while (fNextToken && !fAtEndOfLine && ContinueParse());
}

// cram/auth response data ::= "+" SPACE challenge CRLF
// the server expects more client data after issuing its challenge

void nsImapServerResponseParser::authChallengeResponse_data()
{
  AdvanceToNextToken();
  fAuthChallenge = strdup(fNextToken);
  fWaitingForMoreClientInput = PR_TRUE; 
  
  skip_to_CRLF();
}


void nsImapServerResponseParser::namespace_data()
{
	EIMAPNamespaceType namespaceType = kPersonalNamespace;
	PRBool namespacesCommitted = PR_FALSE;
  const char* serverKey = fServerConnection.GetImapServerKey();
	while ((namespaceType != kUnknownNamespace) && ContinueParse())
	{
		AdvanceToNextToken();
		while (fAtEndOfLine && ContinueParse())
			AdvanceToNextToken();
		if (!PL_strcasecmp(fNextToken,"NIL"))
		{
			// No namespace for this type.
			// Don't add anything to the Namespace object.
		}
		else if (fNextToken[0] == '(')
		{
			// There may be multiple namespaces of the same type.
			// Go through each of them and add them to our Namespace object.

			fNextToken++;
			while (fNextToken[0] == '(' && ContinueParse())
			{
				// we have another namespace for this namespace type
				fNextToken++;
				if (fNextToken[0] != '"')
				{
					SetSyntaxError(PR_TRUE);
				}
				else
				{
					char *namespacePrefix = CreateQuoted(PR_FALSE);

					AdvanceToNextToken();
					const char *quotedDelimiter = fNextToken;
					char namespaceDelimiter = '\0';

					if (quotedDelimiter[0] == '"')
					{
						quotedDelimiter++;
						namespaceDelimiter = quotedDelimiter[0];
					}
					else if (!PL_strncasecmp(quotedDelimiter, "NIL", 3))
					{
						// NIL hierarchy delimiter.  Leave namespace delimiter nsnull.
					}
					else
					{
						// not quoted or NIL.
						SetSyntaxError(PR_TRUE);
					}
					if (ContinueParse())
					{
            // add code to parse the TRANSLATE attribute if it is present....
            // we'll also need to expand the name space code to take in the translated prefix name.

						nsIMAPNamespace *newNamespace = new nsIMAPNamespace(namespaceType, namespacePrefix, namespaceDelimiter, PR_FALSE);
						// add it to a temporary list in the host
						if (newNamespace && fHostSessionList)
							fHostSessionList->AddNewNamespaceForHost(
                                serverKey, newNamespace);

						skip_to_close_paren();	// Ignore any extension data
	
						PRBool endOfThisNamespaceType = (fNextToken[0] == ')');
						if (!endOfThisNamespaceType && fNextToken[0] != '(')	// no space between namespaces of the same type
						{
							SetSyntaxError(PR_TRUE);
						}
					}
					PR_Free(namespacePrefix);
				}
			}
		}
		else
		{
			SetSyntaxError(PR_TRUE);
		}
		switch (namespaceType)
		{
		case kPersonalNamespace:
			namespaceType = kOtherUsersNamespace;
			break;
		case kOtherUsersNamespace:
			namespaceType = kPublicNamespace;
			break;
		default:
			namespaceType = kUnknownNamespace;
			break;
		}
	}
	if (ContinueParse())
	{
		nsImapProtocol *navCon = &fServerConnection;
		NS_ASSERTION(navCon, "null protocol connection while parsing namespace");	// we should always have this
		if (navCon)
		{
			navCon->CommitNamespacesForHostEvent();
			namespacesCommitted = PR_TRUE;
		}
	}
	skip_to_CRLF();

	if (!namespacesCommitted && fHostSessionList)
	{
		PRBool success;
		fHostSessionList->FlushUncommittedNamespacesForHost(serverKey,
                                                            success);
	}
}

void nsImapServerResponseParser::myrights_data(PRBool unsolicited)
{
  AdvanceToNextToken();
  if (ContinueParse() && !fAtEndOfLine)
  {
    char *mailboxName;
    // an unsolicited myrights response won't have the mailbox name in
    // the response, so we use the selected mailbox name.
    if (unsolicited)
    {
      mailboxName = strdup(fSelectedMailboxName);
    }
    else
    {
      mailboxName = CreateAstring();
      if (mailboxName)
        AdvanceToNextToken();
    }
    if (mailboxName)
    {
      if (ContinueParse())
      {
        char *myrights = CreateAstring();
        if (myrights)
        {
          nsImapProtocol *navCon = &fServerConnection;
          NS_ASSERTION(navCon, "null connection parsing my rights"); // we should always have this
          if (navCon)
            navCon->AddFolderRightsForUser(mailboxName, nsnull /* means "me" */, myrights);
          PR_Free(myrights);
        }
        else
        {
          HandleMemoryFailure();
        }
        if (ContinueParse())
          AdvanceToNextToken();
      }
      PR_Free(mailboxName);
    }
    else
    {
      HandleMemoryFailure();
    }
  }
  else
  {
    SetSyntaxError(PR_TRUE);
  }
}

void nsImapServerResponseParser::acl_data()
{
  AdvanceToNextToken();
  if (ContinueParse() && !fAtEndOfLine)
  {
    char *mailboxName = CreateAstring();	// PL_strdup(fNextToken);
    if (mailboxName && ContinueParse())
    {
      AdvanceToNextToken();
      while (ContinueParse() && !fAtEndOfLine)
      {
        char *userName = CreateAstring(); // PL_strdup(fNextToken);
        if (userName && ContinueParse())
        {
          AdvanceToNextToken();
          if (ContinueParse())
          {
            char *rights = CreateAstring(); // PL_strdup(fNextToken);
            if (rights)
            {
              fServerConnection.AddFolderRightsForUser(mailboxName, userName, rights);
              PR_Free(rights);
            }
            else
              HandleMemoryFailure();
            
            if (ContinueParse())
              AdvanceToNextToken();
          }
          PR_Free(userName);
        }
        else
          HandleMemoryFailure();
      }
      PR_Free(mailboxName);
    }
    else
      HandleMemoryFailure();
  }
}


void nsImapServerResponseParser::mime_data()
{
  if (PL_strstr(fNextToken, "MIME"))
    mime_header_data();
  else
    mime_part_data();
}

// mime_header_data should not be streamed out;  rather, it should be
// buffered in the nsIMAPBodyShell.
// This is because we are still in the process of generating enough
// information from the server (such as the MIME header's size) so that
// we can construct the final output stream.
void nsImapServerResponseParser::mime_header_data()
{
  char *partNumber = PL_strdup(fNextToken);
  if (partNumber)
  {
    char *start = partNumber+5, *end = partNumber+5;	// 5 == nsCRT::strlen("BODY[")
    while (ContinueParse() && end && *end != 'M' && *end != 'm')
    {
      end++;
    }
    if (end && (*end == 'M' || *end == 'm'))
    {
      *(end-1) = 0;
      AdvanceToNextToken();
      char *mimeHeaderData = CreateAstring();	// is it really this simple?
      AdvanceToNextToken();
      if (m_shell)
      {
        m_shell->AdoptMimeHeader(start, mimeHeaderData);
      }
    }
    else
    {
      SetSyntaxError(PR_TRUE);
    }
    PR_Free(partNumber);	// partNumber is not adopted by the body shell.
  }
  else
  {
    HandleMemoryFailure();
  }
}

// Actual mime parts are filled in on demand (either from shell generation
// or from explicit user download), so we need to stream these out.
void nsImapServerResponseParser::mime_part_data()
{
  char *checkOriginToken = PL_strdup(fNextToken);
  if (checkOriginToken)
  {
    PRUint32 origin = 0;
    PRBool originFound = PR_FALSE;
    char *whereStart = PL_strchr(checkOriginToken, '<');
    if (whereStart)
    {
      char *whereEnd = PL_strchr(whereStart, '>');
      if (whereEnd)
      {
        *whereEnd = 0;
        whereStart++;
        origin = atoi(whereStart);
        originFound = PR_TRUE;
      }
    }
    PR_Free(checkOriginToken);
    AdvanceToNextToken();
    msg_fetch_content(originFound, origin, MESSAGE_RFC822);	// keep content type as message/rfc822, even though the
    // MIME part might not be, because then libmime will
    // still handle and decode it.
  }
  else
    HandleMemoryFailure();
}

// parse FETCH BODYSTRUCTURE response, "a parenthesized list that describes
// the [MIME-IMB] body structure of a message" [RFC 3501].
void nsImapServerResponseParser::bodystructure_data()
{
  AdvanceToNextToken();
  if (ContinueParse() && fNextToken && *fNextToken == '(')  // It has to start with an open paren.
  {
    // Turn the BODYSTRUCTURE response into a form that the nsIMAPBodypartMessage can be constructed from.
    // FIXME: Follow up on bug 384210 to investigate why the caller has to duplicate the two in-param strings.
    nsIMAPBodypartMessage *message = new nsIMAPBodypartMessage(NULL, NULL, PR_TRUE,
                                                               strdup("message"), strdup("rfc822"),
                                                               NULL, NULL, NULL, 0);
    nsIMAPBodypart *body = bodystructure_part(PL_strdup("1"), message);
    if (body)
      message->SetBody(body);
    else
    {
      delete message;
      message = nsnull;
    }
    m_shell = new nsIMAPBodyShell(&fServerConnection, message, CurrentResponseUID(), GetSelectedMailboxName());
    // ignore syntax errors in parsing the body structure response. If there's an error
    // we'll just fall back to fetching the whole message.
    SetSyntaxError(PR_FALSE);
  }
  else
    SetSyntaxError(PR_TRUE);
}

// RFC3501:  body = "(" (body-type-1part / body-type-mpart) ")"
nsIMAPBodypart *
nsImapServerResponseParser::bodystructure_part(char *partNum, nsIMAPBodypart *parentPart)
{
  // Check to see if this buffer is a leaf or container
  // (Look at second character - if an open paren, then it is a container)
  if (*fNextToken != '(')
  {
    NS_ASSERTION(PR_FALSE, "bodystructure_part must begin with '('");
    return NULL;
  }
  
  if (fNextToken[1] == '(')
    return bodystructure_multipart(partNum, parentPart);
  else
    return bodystructure_leaf(partNum, parentPart);
}

// RFC3501: body-type-1part = (body-type-basic / body-type-msg / body-type-text)
//                            [SP body-ext-1part]
nsIMAPBodypart *
nsImapServerResponseParser::bodystructure_leaf(char *partNum, nsIMAPBodypart *parentPart) 
{
  // historical note: this code was originally in nsIMAPBodypartLeaf::ParseIntoObjects()
  char *bodyType = nsnull, *bodySubType = nsnull, *bodyID = nsnull, *bodyDescription = nsnull, *bodyEncoding = nsnull;
  PRInt32 partLength = 0;
  PRBool isValid = PR_TRUE;
  
  // body type  ("application", "text", "image", etc.)
  if (ContinueParse())
  {
    fNextToken++; // eat the first '('
    bodyType = CreateNilString();
    if (ContinueParse())
      AdvanceToNextToken();
  }
  
  // body subtype  ("gif", "html", etc.)
  if (isValid && ContinueParse())
  {
    bodySubType = CreateNilString();
    if (ContinueParse())
      AdvanceToNextToken();
  }
  
  // body parameter: parenthesized list
  if (isValid && ContinueParse())
  {
    if (fNextToken[0] == '(')
    {
      fNextToken++;
      skip_to_close_paren();
    }
    else if (!PL_strcasecmp(fNextToken, "NIL"))
      AdvanceToNextToken();
  }
  
  // body id
  if (isValid && ContinueParse())
  {
    bodyID = CreateNilString();
    if (ContinueParse())
      AdvanceToNextToken();
  }
  
  // body description
  if (isValid && ContinueParse())
  {
    bodyDescription = CreateNilString();
    if (ContinueParse())
      AdvanceToNextToken();
  }
  
  // body encoding
  if (isValid && ContinueParse())
  {
    bodyEncoding = CreateNilString();
    if (ContinueParse())
      AdvanceToNextToken();
  }
  
  // body size
  if (isValid && ContinueParse())
  {
    char *bodySizeString = CreateAtom();
    if (!bodySizeString)
      isValid = PR_FALSE;
    else
    {
      partLength = atoi(bodySizeString);
      PR_Free(bodySizeString);
      if (ContinueParse())
        AdvanceToNextToken();
    }
  }

  if (!isValid || !ContinueParse())
  {
    PR_FREEIF(partNum);
    PR_FREEIF(bodyType);
    PR_FREEIF(bodySubType);
    PR_FREEIF(bodyID);
    PR_FREEIF(bodyDescription);
    PR_FREEIF(bodyEncoding);
  }
  else
  {
    if (PL_strcasecmp(bodyType, "message") || PL_strcasecmp(bodySubType, "rfc822"))
    {
      skip_to_close_paren();
      return new nsIMAPBodypartLeaf(partNum, parentPart, bodyType, bodySubType,
          bodyID, bodyDescription, bodyEncoding, partLength);
    }
    
    // This part is of type "message/rfc822"  (probably a forwarded message)
    nsIMAPBodypartMessage *message = new nsIMAPBodypartMessage(partNum, parentPart, PR_FALSE,
      bodyType, bodySubType, bodyID, bodyDescription, bodyEncoding, partLength);

    // there are three additional fields: envelope structure, bodystructure, and size in lines    
    // historical note: this code was originally in nsIMAPBodypartMessage::ParseIntoObjects()

    // envelope (ignored)
    if (*fNextToken == '(')
    {
      fNextToken++;
      skip_to_close_paren();
    }
    else
      isValid = PR_FALSE;

    // bodystructure
    if (isValid && ContinueParse())
    {
      if (*fNextToken != '(')
        isValid = PR_FALSE;
      else
      {
        char *bodyPartNum = PR_smprintf("%s.1", partNum);
        if (bodyPartNum)
        {
          nsIMAPBodypart *body = bodystructure_part(bodyPartNum, message);
          if (body)
            message->SetBody(body);
          else
            isValid = PR_FALSE;
        }
      }
    }
    
    // ignore "size in text lines"

    if (isValid && ContinueParse()) {
      skip_to_close_paren();
      return message;
    }
    delete message;
  }

  // parsing failed, just move to the end of the parentheses group
  if (ContinueParse())
    skip_to_close_paren();
  return nsnull;
}


// RFC3501:  body-type-mpart = 1*body SP media-subtype
//                             [SP body-ext-mpart]
nsIMAPBodypart *
nsImapServerResponseParser::bodystructure_multipart(char *partNum, nsIMAPBodypart *parentPart) 
{
  nsIMAPBodypartMultipart *multipart = new nsIMAPBodypartMultipart(partNum, parentPart);
  PRBool isValid = multipart->GetIsValid();
  // historical note: this code was originally in nsIMAPBodypartMultipart::ParseIntoObjects()
  if (ContinueParse())
  {
    fNextToken++; // eat the first '('
    // Parse list of children
    int childCount = 0;
    while (isValid && fNextToken[0] == '(' && ContinueParse())
    {
      childCount++;
      char *childPartNum = NULL;
      // note: the multipart constructor does some magic on partNumber
      if (PL_strcmp(multipart->GetPartNumberString(), "0")) // not top-level
        childPartNum = PR_smprintf("%s.%d", multipart->GetPartNumberString(), childCount);
      else // top-level
        childPartNum = PR_smprintf("%d", childCount);
      if (!childPartNum)
        isValid = PR_FALSE;
      else
      {
        nsIMAPBodypart *child = bodystructure_part(childPartNum, multipart);
        if (child)
          multipart->AppendPart(child);
        else
          isValid = PR_FALSE;
      }
    }

    // RFC3501:  media-subtype   = string
    // (multipart subtype: mixed, alternative, etc.)
    if (isValid && ContinueParse())
    {
      char *bodySubType = CreateNilString();
      multipart->SetBodySubType(bodySubType);
      if (ContinueParse())
        AdvanceToNextToken();
    }

    // extension data:
    // RFC3501:  body-ext-mpart = body-fld-param [SP body-fld-dsp [SP body-fld-lang
    //                            [SP body-fld-loc *(SP body-extension)]]]
    
    // body parameter parenthesized list (optional data), includes boundary parameter
    // RFC3501:  body-fld-param  = "(" string SP string *(SP string SP string) ")" / nil
    char *boundaryData = nsnull;
    if (isValid && ContinueParse() && *fNextToken == '(')
    {
      fNextToken++;
      while (ContinueParse() && *fNextToken != ')')
      {
        char *attribute = CreateNilString();
        if (ContinueParse())
          AdvanceToNextToken();
        if (ContinueParse() && !PL_strcasecmp(attribute, "BOUNDARY"))
        {
          char *boundary = CreateNilString();
          if (boundary)
            boundaryData = PR_smprintf("--%s", boundary);
          PR_FREEIF(boundary);
        }
        else if (ContinueParse())
        {
          char *value = CreateNilString();
          PR_FREEIF(value);
        }
        PR_FREEIF(attribute);
        if (ContinueParse())
          AdvanceToNextToken();
      }
      if (ContinueParse())
        fNextToken++;  // skip closing ')'
    }
    if (boundaryData)
      multipart->SetBoundaryData(boundaryData);
    else  
      isValid = PR_FALSE;   // Actually, we should probably generate a boundary here.
  }

  // always move to closing ')', even if part was not successfully read 
  if (ContinueParse())
    skip_to_close_paren();

  if (isValid)
    return multipart;
  delete multipart;
  return nsnull;
}


// RFC2087:  quotaroot_response = "QUOTAROOT" SP astring *(SP astring)
//           quota_response = "QUOTA" SP astring SP quota_list
//           quota_list     = "(" [quota_resource *(SP quota_resource)] ")"
//           quota_resource = atom SP number SP number
// Only the STORAGE resource is considered.  The current implementation is
// slightly broken because it assumes that STORAGE is the first resource;
// a reponse   QUOTA (MESSAGE 5 100 STORAGE 10 512)   would be ignored.
void nsImapServerResponseParser::quota_data()
{
  if (!PL_strcasecmp(fNextToken, "QUOTAROOT"))
  {
    // ignore QUOTAROOT response
    nsCString quotaroot; 
    AdvanceToNextToken();
    while (ContinueParse() && !fAtEndOfLine)
    {
      quotaroot.Adopt(CreateAstring());
      AdvanceToNextToken();
    }
  }
  else if(!PL_strcasecmp(fNextToken, "QUOTA"))
  {
    PRUint32 used, max;
    char *parengroup;

    AdvanceToNextToken();
    if (ContinueParse())
    {
      nsCString quotaroot;
      quotaroot.Adopt(CreateAstring());

      if(ContinueParse() && !fAtEndOfLine)
      {
        AdvanceToNextToken();
        if(fNextToken)
        {
          if(!PL_strcasecmp(fNextToken, "(STORAGE"))
          {
            parengroup = CreateParenGroup();
            if(parengroup && (PR_sscanf(parengroup, "(STORAGE %lu %lu)", &used, &max) == 2) )
            {
              fServerConnection.UpdateFolderQuotaData(quotaroot, used, max);
              skip_to_CRLF();
            }
            else
              SetSyntaxError(PR_TRUE);

            PR_Free(parengroup);
          }
          else
            // Ignore other limits, we just check STORAGE for now
            skip_to_CRLF();
        }
        else
          SetSyntaxError(PR_TRUE);
      }
      else
        HandleMemoryFailure();
    }
  }
  else
    SetSyntaxError(PR_TRUE);
}

PRBool nsImapServerResponseParser::GetFillingInShell()
{
  return (m_shell != nsnull);
}

PRBool nsImapServerResponseParser::GetDownloadingHeaders()
{
  return fDownloadingHeaders;
}

// Tells the server state parser to use a previously cached shell.
void	nsImapServerResponseParser::UseCachedShell(nsIMAPBodyShell *cachedShell)
{
	// We shouldn't already have another shell we're dealing with.
	if (m_shell && cachedShell)
	{
		PR_LOG(IMAP, PR_LOG_ALWAYS, ("PARSER: Shell Collision"));
		NS_ASSERTION(PR_FALSE, "shell collision");
	}
	m_shell = cachedShell;
}


void nsImapServerResponseParser::ResetCapabilityFlag() 
{
    if (fHostSessionList)
    {
        fHostSessionList->SetCapabilityForHost(
            fServerConnection.GetImapServerKey(), kCapabilityUndefined);
    }
}

/*
 literal         ::= "{" number "}" CRLF *CHAR8
                              ;; Number represents the number of CHAR8 octets
*/
// returns PR_TRUE if this is the last chunk and we should close the stream
PRBool nsImapServerResponseParser::msg_fetch_literal(PRBool chunk, PRInt32 origin)
{
  numberOfCharsInThisChunk = atoi(fNextToken + 1); // might be the whole message
  charsReadSoFar = 0;
  static PRBool lastCRLFwasCRCRLF = PR_FALSE;
  
  PRBool lastChunk = !chunk || (origin + numberOfCharsInThisChunk >= fTotalDownloadSize);
  
  nsImapAction imapAction; 
  if (!fServerConnection.GetCurrentUrl())
    return PR_TRUE;
  fServerConnection.GetCurrentUrl()->GetImapAction(&imapAction);
  if (!lastCRLFwasCRCRLF && 
    fServerConnection.GetIOTunnellingEnabled() && 
    (numberOfCharsInThisChunk > fServerConnection.GetTunnellingThreshold()) &&
    (imapAction != nsIImapUrl::nsImapOnlineToOfflineCopy) &&
    (imapAction != nsIImapUrl::nsImapOnlineToOfflineMove))
  {
    // One day maybe we'll make this smarter and know how to handle CR/LF boundaries across tunnels.
    // For now, we won't, even though it might not be too hard, because it is very rare and will add
    // some complexity.
    charsReadSoFar = fServerConnection.OpenTunnel(numberOfCharsInThisChunk);
  }
  // If we're fetching the whole message, the length of the returned literal
  // must be the message size, and for servers like Exchange that only
  // approximate the rfc822 size, we can use this size as the correct size.
  if (!chunk && fFetchEverythingRFC822)
    fSizeOfMostRecentMessage = numberOfCharsInThisChunk;
  
  // If we opened a tunnel, finish everything off here.  Otherwise, get everything here.
  // (just like before)
  
  while (ContinueParse() && !fServerConnection.DeathSignalReceived() && (charsReadSoFar < numberOfCharsInThisChunk))
  {
    AdvanceToNextLine();
    if (ContinueParse())
    {
      if (lastCRLFwasCRCRLF && (*fCurrentLine == '\r'))
      {
        char *usableCurrentLine = PL_strdup(fCurrentLine + 1);
        PR_Free(fCurrentLine);
        fCurrentLine = usableCurrentLine;
      }
      
      if (ContinueParse())
      {
        charsReadSoFar += strlen(fCurrentLine);
        if (!fDownloadingHeaders && fCurrentCommandIsSingleMessageFetch)
        {
          fServerConnection.ProgressEventFunctionUsingId(IMAP_DOWNLOADING_MESSAGE);
          if (fTotalDownloadSize > 0)
            fServerConnection.PercentProgressUpdateEvent(0,charsReadSoFar + origin, fTotalDownloadSize);
        }
        if (charsReadSoFar > numberOfCharsInThisChunk)
        {	// this is rare.  If this msg ends in the middle of a line then only display the actual message.
          char *displayEndOfLine = (fCurrentLine + strlen(fCurrentLine) - (charsReadSoFar - numberOfCharsInThisChunk));
          char saveit = *displayEndOfLine;
          *displayEndOfLine = 0;
          fServerConnection.HandleMessageDownLoadLine(fCurrentLine, !lastChunk);
          *displayEndOfLine = saveit;
          lastCRLFwasCRCRLF = (*(displayEndOfLine - 1) == '\r');
        }
        else
        {
          lastCRLFwasCRCRLF = (*(fCurrentLine + strlen(fCurrentLine) - 1) == '\r');
          fServerConnection.HandleMessageDownLoadLine(fCurrentLine, !lastChunk && (charsReadSoFar == numberOfCharsInThisChunk), fCurrentLine);
        }
      }
    }
  }
  
  // This would be a good thing to log.
  if (lastCRLFwasCRCRLF)
    PR_LOG(IMAP, PR_LOG_ALWAYS, ("PARSER: CR/LF fell on chunk boundary."));
  
  if (ContinueParse())
  {
    if (charsReadSoFar > numberOfCharsInThisChunk)
    {
      // move the lexical analyzer state to the end of this message because this message
      // fetch ends in the middle of this line.
      //fCurrentTokenPlaceHolder = fLineOfTokens + nsCRT::strlen(fCurrentLine) - (charsReadSoFar - numberOfCharsInThisChunk);
      AdvanceTokenizerStartingPoint(strlen(fCurrentLine) - (charsReadSoFar - numberOfCharsInThisChunk));
      AdvanceToNextToken();
    }
    else
    {
      skip_to_CRLF();
      AdvanceToNextToken();
    }
  }
  else
  {
    lastCRLFwasCRCRLF = PR_FALSE;
  }
  return lastChunk;
}

PRBool nsImapServerResponseParser::CurrentFolderReadOnly()
{
  return fCurrentFolderReadOnly;
}

PRInt32	nsImapServerResponseParser::NumberOfMessages()
{
  return fNumberOfExistingMessages;
}

PRInt32 nsImapServerResponseParser::NumberOfRecentMessages()
{
  return fNumberOfRecentMessages;
}

PRInt32 nsImapServerResponseParser::NumberOfUnseenMessages()
{
  return fNumberOfUnseenMessages;
}

PRInt32 nsImapServerResponseParser::FolderUID()
{
  return fFolderUIDValidity;
}

void nsImapServerResponseParser::SetCurrentResponseUID(PRUint32 uid)
{
  if (uid > 0)
    fCurrentResponseUID = uid;
}

PRUint32 nsImapServerResponseParser::CurrentResponseUID()
{
  return fCurrentResponseUID;
}

PRUint32 nsImapServerResponseParser::HighestRecordedUID()
{
  return fHighestRecordedUID;
}

PRBool nsImapServerResponseParser::IsNumericString(const char *string)
{
  int i;
  for(i = 0; i < (int) PL_strlen(string); i++)
  {
    if (! isdigit(string[i]))
    {
      return PR_FALSE;
    }
  }
  
  return PR_TRUE;
}


nsImapMailboxSpec *nsImapServerResponseParser::CreateCurrentMailboxSpec(const char *mailboxName /* = nsnull */)
{
  nsImapMailboxSpec *returnSpec = new nsImapMailboxSpec;
  if (!returnSpec)
  {
    HandleMemoryFailure();
    return  nsnull;
  }
  NS_ADDREF(returnSpec);
  const char *mailboxNameToConvert = (mailboxName) ? mailboxName : fSelectedMailboxName;
  if (mailboxNameToConvert)
  {
    const char *serverKey = fServerConnection.GetImapServerKey();
    nsIMAPNamespace *ns = nsnull;
    if (serverKey && fHostSessionList)
      fHostSessionList->GetNamespaceForMailboxForHost(serverKey, mailboxNameToConvert, ns);	// for
      // delimiter
    returnSpec->mHierarchySeparator = (ns) ? ns->GetDelimiter(): '/';
    
  }
  
  returnSpec->mFolderSelected = !mailboxName; // if mailboxName is null, we're doing a Status
  returnSpec->mFolder_UIDVALIDITY = fFolderUIDValidity;
  returnSpec->mHighestModSeq = fHighestModSeq;
  returnSpec->mNumOfMessages = (mailboxName) ? fStatusExistingMessages : fNumberOfExistingMessages;
  returnSpec->mNumOfUnseenMessages = (mailboxName) ? fStatusUnseenMessages : fNumberOfUnseenMessages;
  returnSpec->mNumOfRecentMessages = (mailboxName) ? fStatusRecentMessages : fNumberOfRecentMessages;
  returnSpec->mNextUID = fStatusNextUID;

  returnSpec->mSupportedUserFlags = fSupportsUserDefinedFlags;

  returnSpec->mBoxFlags = kNoFlags;	// stub
  returnSpec->mOnlineVerified = PR_FALSE;	// we're fabricating this.  The flags aren't verified.
  returnSpec->mAllocatedPathName.Assign(mailboxNameToConvert);
  returnSpec->mConnection = &fServerConnection;
  if (returnSpec->mConnection)
  {
    nsIURI * aUrl = nsnull;
    nsresult rv = NS_OK;
    returnSpec->mConnection->GetCurrentUrl()->QueryInterface(NS_GET_IID(nsIURI), (void **) &aUrl);
    if (NS_SUCCEEDED(rv) && aUrl) 
      aUrl->GetHost(returnSpec->mHostName);
    
    NS_IF_RELEASE(aUrl);
  }
  else
    returnSpec->mHostName.Truncate();

  if (fFlagState)
    returnSpec->mFlagState = fFlagState; //copies flag state
  else
    returnSpec->mFlagState = nsnull;
  
  return returnSpec;
  
}
// Reset the flag state.
void nsImapServerResponseParser::ResetFlagInfo()
{
  if (fFlagState)
    fFlagState->Reset();
}


PRBool nsImapServerResponseParser::GetLastFetchChunkReceived()
{
  return fLastChunk;
}

void nsImapServerResponseParser::ClearLastFetchChunkReceived()
{
  fLastChunk = PR_FALSE;
}

void nsImapServerResponseParser::SetHostSessionList(nsIImapHostSessionList*
                                               aHostSessionList)
{
    NS_IF_RELEASE (fHostSessionList);
    fHostSessionList = aHostSessionList;
    NS_IF_ADDREF (fHostSessionList);
}

void nsImapServerResponseParser::SetSyntaxError(PRBool error, const char *msg)
{
  nsIMAPGenericParser::SetSyntaxError(error, msg);
  if (error)
  {
    if (!fCurrentLine)
    {
      HandleMemoryFailure();
      fServerConnection.Log("PARSER", ("Internal Syntax Error: %s: <no line>"), msg);
    }
    else
    {
      if (!strcmp(fCurrentLine, CRLF))
        fServerConnection.Log("PARSER", "Internal Syntax Error: %s: <CRLF>", msg);
      else
      {
        if (msg)
          fServerConnection.Log("PARSER", "Internal Syntax Error: %s:", msg);
        fServerConnection.Log("PARSER", "Internal Syntax Error on line: %s", fCurrentLine);
      }      
    }
  }
}

nsresult nsImapServerResponseParser::BeginMessageDownload(const char *content_type)
{
  // if we're downloading a message, assert that we know its size.
  NS_ASSERTION(fDownloadingHeaders || fSizeOfMostRecentMessage > 0, "most recent message has 0 or negative size");
  nsresult rv = fServerConnection.BeginMessageDownLoad(fSizeOfMostRecentMessage, 
    content_type);
  if (NS_FAILED(rv))
  {
    skip_to_CRLF();
    fServerConnection.PseudoInterrupt(PR_TRUE);
    fServerConnection.AbortMessageDownLoad();
  }
  return rv;
}
