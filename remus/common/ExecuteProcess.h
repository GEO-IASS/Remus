//=============================================================================
//
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//=============================================================================

#ifndef __remus_common__ExecuteProcess_h
#define __remus_common__ExecuteProcess_h

#include <set>
#include <string>
#include <vector>

#include "CommonExports.h"

//forward declare the systools

namespace remus{
namespace common{


struct REMUSCOMMON_EXPORT ProcessPipe
{
  enum PipeType
    {
    None,
    STDIN,
    STDOUT,
    STDERR,
    Timeout
    };

  explicit ProcessPipe(ProcessPipe::PipeType pType):
    type(pType),text(){}

  bool valid() const { return type != None && type != Timeout; }

  ProcessPipe::PipeType type;
  std::string text;
};


class REMUSCOMMON_EXPORT ExecuteProcess
{
public:

  enum DetachMode
  {
    Detached,
    NotDetached
  };

  ExecuteProcess(const std::string& command, const std::vector<std::string>& args);
  explicit ExecuteProcess(const std::string& command);

  //Will terminate the external process if has been started
  //and wasn't set to run in detached mode
  virtual ~ExecuteProcess();

  //execute the process. set detach to Detached if you don't want to receive
  //any output from the child process. Be sure not to poll on a detached
  //process as it won't work
  virtual void execute(DetachMode mode);

  //kills the process if running
  virtual bool kill();

  //returns if the process is still alive
  bool isAlive();

  //returns if the process exited normally.
  //If the process is still running, killed, disowned, not yet running
  //this will return false
  bool exitedNormally();

  //Will poll for a given timeout value looking any output on the STDIN,STDOUT,and
  //STDERR streams.
  //The timeout's unit of time is SECONDS.
  //If the value of timeout is zero or greater we will wait that duration.
  //If the value of timeout is negative we will  block indefinitely until a
  //output on one of the pipes happens
  remus::common::ProcessPipe poll(double timeout);

private:
  ExecuteProcess(const ExecuteProcess&);
  void operator=(const ExecuteProcess&);

  std::string Command;
  std::vector<std::string> Args;

  struct Process;
  Process* ExternalProcess;
};

}
}
#endif
