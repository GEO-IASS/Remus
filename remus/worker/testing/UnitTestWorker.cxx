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

#include <remus/server/PortNumbers.h>
#include <remus/common/SleepFor.h>
#include <remus/proto/zmqHelper.h>
#include <remus/worker/ServerConnection.h>
#include <remus/worker/Worker.h>

#include <remus/testing/Testing.h>

REMUS_THIRDPARTY_PRE_INCLUDE
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>
REMUS_THIRDPARTY_POST_INCLUDE


#include <string>

namespace {

zmq::socketInfo<zmq::proto::tcp> make_tcp_socket(std::string host, int port)
{
  return zmq::socketInfo<zmq::proto::tcp>(host,port);
}

zmq::socketInfo<zmq::proto::ipc> make_ipc_socket(std::string host)
{
  return zmq::socketInfo<zmq::proto::ipc>(host);
}

zmq::socketInfo<zmq::proto::inproc> make_inproc_socket(std::string host)
{
  return zmq::socketInfo<zmq::proto::inproc>(host);
}

class fake_server
{
public:
  template<typename ProtoType>
  fake_server(zmq::socketInfo<ProtoType>& conn,
               boost::shared_ptr<zmq::context_t> context):
    WorkerComm((*context),ZMQ_ROUTER),
    PollingThread( new boost::thread() ),
    ContinuePolling(true)
  {
    zmq::bindToAddress(this->WorkerComm, conn);

    //start up our thread
    boost::scoped_ptr<boost::thread> pollingThread(
                             new boost::thread( &fake_server::poll, this) );
    //transfer ownership of the polling thread to our scoped_ptr
    this->PollingThread.swap(pollingThread);
  }

  ~fake_server()
  {
    this->ContinuePolling = false;
    this->PollingThread->join();
  }

private:
  void poll()
  {
    zmq::pollitem_t item  = { this->WorkerComm,  0, ZMQ_POLLIN, 0 };
    while( this->ContinuePolling )
      {
      zmq::poll_safely(&item,1,250);
      }
  }

  zmq::socket_t WorkerComm;
  boost::scoped_ptr<boost::thread> PollingThread;
  bool ContinuePolling;
};

void verify_server_connection_tcpip()
{
  using namespace remus::meshtypes;
  const remus::common::MeshIOType mtype =
                          remus::common::make_MeshIOType(Model(),Model());

  zmq::socketInfo<zmq::proto::tcp> local_socket("127.0.0.1",
                                        remus::server::WORKER_PORT+101);
  remus::worker::ServerConnection tcp_ip_conn(local_socket);

  //start up server to talk to worker
  fake_server fake_def_server(local_socket, tcp_ip_conn.context());
  remus::worker::Worker default_worker(mtype,tcp_ip_conn);

  const remus::worker::ServerConnection& sc = default_worker.connection();

  REMUS_ASSERT( (sc.endpoint().size() > 0) );
  REMUS_ASSERT( (sc.endpoint() == local_socket.endpoint()) );
  REMUS_ASSERT( (sc.endpoint() ==
       make_tcp_socket("127.0.0.1",remus::server::WORKER_PORT+101).endpoint()) );

  REMUS_ASSERT( (sc.isLocalEndpoint()==true) );
}

void verify_server_connection_inproc()
{
  using namespace remus::meshtypes;
  const remus::common::MeshIOType mtype =
                          remus::common::make_MeshIOType(Model(),Model());

  zmq::socketInfo<zmq::proto::inproc> inproc_info("foo_inproc");
  remus::worker::ServerConnection inproc_conn(inproc_info);

  fake_server inproc_server(inproc_info, inproc_conn.context());
  remus::worker::Worker inproc_worker(mtype,inproc_conn);

  REMUS_ASSERT( (inproc_worker.connection().endpoint() ==
                 make_inproc_socket("foo_inproc").endpoint()) );

  //share a connection between two workers that share the same context and
  //channel. This shows that you can have multiple works sharing the same
  //inproc context.
  remus::worker::ServerConnection conn2 =
    remus::worker::make_ServerConnection(inproc_worker.connection().endpoint());
  conn2.context(inproc_conn.context());


  remus::worker::Worker inproc_worker2(mtype,conn2);
  REMUS_ASSERT( (inproc_worker2.connection().endpoint() ==
                 make_inproc_socket("foo_inproc").endpoint()) );
  REMUS_ASSERT( (inproc_worker.connection().context() ==
                 inproc_worker2.connection().context() ) );

  REMUS_ASSERT( (inproc_worker.connection().isLocalEndpoint()==true) );
}

void verify_server_connection_ipc()
{
  using namespace remus::meshtypes;
  const remus::common::MeshIOType mtype =
                          remus::common::make_MeshIOType(Model(),Model());

  zmq::socketInfo<zmq::proto::ipc> ipc_info("foo_ipc");
  remus::worker::ServerConnection ipc_conn(ipc_info);

  fake_server ipc_server(ipc_info,ipc_conn.context());
  remus::worker::Worker ipc_worker(mtype,ipc_conn);

  REMUS_ASSERT( (ipc_worker.connection().endpoint() ==
                 make_ipc_socket("foo_ipc").endpoint()) );
  REMUS_ASSERT( (ipc_worker.connection().isLocalEndpoint()==true) );

  //share a connection between two workers that share the same context and
  //channel. This shows that you can have multiple works sharing the same
  //ipc context.
  remus::worker::ServerConnection conn2 =
    remus::worker::make_ServerConnection(ipc_worker.connection().endpoint());
  conn2.context(ipc_conn.context());
  remus::worker::Worker ipc_worker2(mtype,conn2);

  REMUS_ASSERT( (ipc_worker2.connection().endpoint() ==
                 make_ipc_socket("foo_ipc").endpoint()) );
  REMUS_ASSERT( (ipc_worker.connection().context() ==
                 ipc_worker2.connection().context() ) );

}

void verify_polling_rates()
{
  using namespace remus::meshtypes;
  const remus::common::MeshIOType mtype =
                          remus::common::make_MeshIOType(Model(),Model());

  zmq::socketInfo<zmq::proto::tcp> local_socket("127.0.0.1",
                                        remus::server::WORKER_PORT+101);
  remus::worker::ServerConnection tcp_ip_conn(local_socket);

  //start up server to talk to worker
  fake_server fake_def_server(local_socket, tcp_ip_conn.context());
  remus::worker::Worker worker(mtype,tcp_ip_conn);

  //verify that we get valid numbers for the polling rates by default
  remus::worker::PollingRates current_rates = worker.pollingRates();
  REMUS_ASSERT( (current_rates.minRate() > 0) )
  REMUS_ASSERT( (current_rates.minRate() < current_rates.maxRate()) )

  //verify that we can't pass negative polling values, but instead
  //clamp to zero
  remus::worker::PollingRates invalid_rates(-4, -20);
  worker.pollingRates( invalid_rates );
  REMUS_ASSERT( (worker.pollingRates().minRate() == 0 ) )
  REMUS_ASSERT( (worker.pollingRates().maxRate() == 0 ) )

  //verify that we properly invert incorrect polling min and maxes
  remus::worker::PollingRates inverted_rates(400, 20);
  worker.pollingRates( inverted_rates );
  REMUS_ASSERT( (worker.pollingRates().minRate() == 20 ) )
  REMUS_ASSERT( (worker.pollingRates().maxRate() == 400 ) )

  //verify that we inverted rates combined with a negative value
  //are inverted and clamped
  remus::worker::PollingRates inverted_negative_rates(100, -20);
  worker.pollingRates( inverted_negative_rates );
  REMUS_ASSERT( (worker.pollingRates().minRate() == 0 ) )
  REMUS_ASSERT( (worker.pollingRates().maxRate() == 100 ) )

  //verify that valid polling rates are kept by the worker
  remus::worker::PollingRates proper_rates(30,120);
  worker.pollingRates( proper_rates );
  REMUS_ASSERT( (worker.pollingRates().minRate() == 30 ) )
  REMUS_ASSERT( (worker.pollingRates().maxRate() == 120 ) )
}

} //namespace


int UnitTestWorker(int, char *[])
{
  verify_server_connection_tcpip();
  verify_server_connection_inproc();
#ifndef _WIN32
  verify_server_connection_ipc();
#endif

  verify_polling_rates();

  //Keep the test running while the OS has time to unbind the sockets, this
  //should help other tests from failing to bind to the now released socket
  remus::common::SleepForMillisec(1000);
  return 0;
}
