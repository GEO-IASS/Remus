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
//  Copyright 2012 Sandia Corporation.
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//
//=============================================================================
#ifndef remus_testing_integration_detail_Factories_h
#define remus_testing_integration_detail_Factories_h

#include <remus/server/WorkerFactoryBase.h>
#include <remus/worker/Worker.h>

// These lines need to come before we include boost ASIO!
// Boost ASIO treats ICC 15+ on OSX as having support for lambda's
// because the clang front-end reports them as supported ( when they aren't ).
// So to work around this we explicit tell boost not to use this code
// when we are compiling as C++98
#include <remus/common/CompilerInformation.h>
#if defined(REMUS_ICC) && !defined(REMUS_HAVE_CXX_11)
#define BOOST_ASIO_DISABLE_MOVE 1
#endif

REMUS_THIRDPARTY_PRE_INCLUDE
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/thread/locks.hpp>
REMUS_THIRDPARTY_POST_INCLUDE

#include <map>

#include <remus/testing/integration/detail/Workers.h>

namespace remus {
namespace testing {
namespace integration {
namespace detail {

  //we want a custom factory that can not create any workers
  //but alays states that it can support a mesh type
  class AlwaysSupportFactory: public remus::server::WorkerFactoryBase
  {
  public:

    AlwaysSupportFactory( std::string workerName ):
      WorkerName(workerName)
    {
    }

    //when asked about which IOTypes we support we say none
    remus::common::MeshIOTypeSet supportedIOTypes() const
    {
      //we need to return that we support all types!
      return remus::common::generateAllIOTypes();
    }

    remus::proto::JobRequirementsSet workerRequirements(
                                            remus::common::MeshIOType type) const
    {
      //make clients think we have real workers, by sending back fake job reqs
      remus::proto::JobRequirements reqs =
           remus::proto::make_JobRequirements(type,WorkerName,"");
      remus::proto::JobRequirementsSet reqSet;
      reqSet.insert(reqs);
      return reqSet;
    }

    bool haveSupport(const remus::proto::JobRequirements& reqs) const
      {
      (void) reqs;
      //we want to return true so that the server adds jobs to it worker queue
      return true;
      }

    bool createWorker(const remus::proto::JobRequirements& type,
                      WorkerFactoryBase::FactoryDeletionBehavior lifespan)
      {
      (void) type;
      (void) lifespan;
      //we want to return false here so that server never thinks we are creating
      //a worker and assigns a job to a worker we didn't create
      return false;
      }

    void updateWorkerCount(){}
    unsigned int currentWorkerCount() const { return 0; }

    std::string WorkerName;
    };


//worker poll based server factory.
//You pass to the factory the requirements of the worker, the max number of
// workers you want.
class ThreadPoolWorkerFactory: public remus::server::WorkerFactoryBase
{
public:
  ThreadPoolWorkerFactory( remus::proto::JobRequirements reqs,
                           std::size_t maxThreadCount ):
    WorkerFactoryBase(),
    WorkerReqs(reqs),
    Connection(),
    HasConnection(false),
    IOService(),
    IOWork( IOService ),
    ThreadPool(),
    Mutex(),
    CurrentWorkerCount( 0 ),
    JobsPerThread()
  {
    this->setMaxWorkerCount( static_cast<unsigned int>(maxThreadCount) );

    for ( std::size_t i = 0; i < maxThreadCount; ++i )
    {
      this->ThreadPool.create_thread(
              boost::bind( &boost::asio::io_service::run,
                           &this->IOService ) );
    }
  }

  ~ThreadPoolWorkerFactory()
    {
    typedef std::map< ::boost::thread::id, std::size_t >::const_iterator c_it;
    for (c_it i = this->JobsPerThread.begin(); i != this->JobsPerThread.end(); ++i )
      {
      std::cout << "thread: " << i->first << " finished " << i->second << " jobs " << std::endl;
      }
    }

  remus::proto::JobRequirementsSet workerRequirements(remus::common::MeshIOType type) const
  {
    remus::proto::JobRequirementsSet result;
    if(type == this->WorkerReqs.meshTypes())
      {
      result.insert( this->WorkerReqs );
      }
    return result;
  }

  remus::common::MeshIOTypeSet supportedIOTypes() const
  {
    remus::common::MeshIOTypeSet result;
    result.insert(this->WorkerReqs.meshTypes());
    return result;
  }

  bool haveSupport(const remus::proto::JobRequirements& reqs) const
  {
    return reqs == this->WorkerReqs;
  }

  bool createWorker(const remus::proto::JobRequirements& reqs,
                    remus::server::WorkerFactoryBase::FactoryDeletionBehavior /*lifespan*/)
  {
    boost::lock_guard< boost::mutex > lock( this->Mutex );
    if(this->CurrentWorkerCount == this->maxWorkerCount())
      {
      return false;
      }
    if( !(reqs == this->WorkerReqs) )
      {
      return false;
      }

    IOService.post( boost::bind( &ThreadPoolWorkerFactory::LaunchWorker, this ) );

    //mark a worker as being used
    this->CurrentWorkerCount++;

    return true;
  }

  void updateWorkerCount()
  {
    //no need to do anything here the
  }

  unsigned int currentWorkerCount() const
  {
    boost::lock_guard< boost::mutex > lock( this->Mutex );
    return this->CurrentWorkerCount;
  }

private:
  void LaunchWorker()
  {
    using namespace remus::testing;
    //construct a shared connection for all workers to use
    if(!this->HasConnection)
      {
      this->Connection = remus::worker::make_ServerConnection(this->workerEndpoint());
      this->HasConnection = true;
      }

    const boost::thread::id t_id = boost::this_thread::get_id();
    std::size_t numCompleted = 0;

    try
      {
      //construct the worker and tell it to process a job
      std::cout << "constructing a worker " << t_id << std::endl;
      integration::detail::SingleShotWorker worker(this->WorkerReqs,
                                                   this->Connection);

      //should be 1, could be zero
      numCompleted = worker.numberOfCompletedJobs();
      }
      catch(...)
      { //the worker crashed, just ignore it
      std::cout << "ERROR: worker factory failed to make a SingleShotWorker!" << std::endl;
      }

    //mark the thread as having finished another job
    if(this->JobsPerThread.find( t_id ) != this->JobsPerThread.end())
      {
      boost::lock_guard< boost::mutex > lock( this->Mutex );
      std::cout << t_id << " finished a job(" << numCompleted << ")"<< std::endl;
      this->JobsPerThread[ t_id ]+= numCompleted;
      }
    else
      {
      boost::lock_guard< boost::mutex > lock( this->Mutex );
      std::cout << t_id << " finished first job(" << numCompleted << ")"<< std::endl;
      this->JobsPerThread[ t_id ] = numCompleted;
      }

    //worker is finished so mark the thread as usable
    {
    boost::lock_guard< boost::mutex > lock( this->Mutex );
    this->CurrentWorkerCount--;
    }
  }

  //when we are quickly starting up and shutting down
  //workers we can run into some zmq edge case bugs, mainly
  //the error Assertion failed: get_load () == 0 (poller_base.cpp:31)
  //The issue is:
  //is sendlarge ZMQ_DONTWAIT message(s)
  //than close the socket and context immediately
  //the destination will never receives anything, and if you do
  //it enough times you get that assertion failure.
  //The solution is to use a single connection so that all
  //workers use the same zmq context
  remus::proto::JobRequirements WorkerReqs;
  remus::worker::ServerConnection Connection;
  bool HasConnection;

  boost::asio::io_service IOService;
  boost::asio::io_service::work IOWork;
  boost::thread_group ThreadPool;

  mutable boost::mutex Mutex;
  volatile unsigned int CurrentWorkerCount;

  std::map< ::boost::thread::id, std::size_t > JobsPerThread;
};

}
}
}
}
#endif //remus_testing_integration_detail_Factories_h
