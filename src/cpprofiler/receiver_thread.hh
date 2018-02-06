#ifndef CPPROFILER_RECEIVER_HH
#define CPPROFILER_RECEIVER_HH

#include <cstdint>
#include <memory>
#include <QThread>

namespace cpprofiler {

class Conductor;
class Message;
class Execution;
class ReceiverWorker;

class ReceiverThread : public QThread {
Q_OBJECT
    const intptr_t m_socket_desc;
    std::unique_ptr<ReceiverWorker> m_worker;

    void run() override;

signals:

    void newExecution(Execution* e);
    void newNode(Message* node);
    void doneReceiving();

public:
    ReceiverThread(intptr_t socket_desc);
    ~ReceiverThread();


};

}

#endif