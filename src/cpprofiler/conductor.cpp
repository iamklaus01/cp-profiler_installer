#include "conductor.hh"
#include "tcp_server.hh"
#include "receiver_thread.hh"
#include <iostream>
#include <thread>
#include <QTreeView>
#include <QGridLayout>
#include <QPushButton>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include "../cpp-integration/message.hpp"

#include "execution.hh"
#include "tree_builder.hh"
#include "execution_list.hh"
#include "execution_window.hh"

#include "analysis/merge_window.hh"
#include "analysis/tree_merger.hh"

#include "utils/std_ext.hh"

#include <random>

#include <QProgressDialog>

#include "name_map.hh"

namespace cpprofiler {

    Conductor::Conductor(Options opt) : m_options(opt) {

        setWindowTitle("CP-Profiler");

        readSettings();

        auto layout = new QGridLayout();

        {
            auto window = new QWidget();
            setCentralWidget(window);
            window->setLayout(layout);
        }

        m_execution_list.reset(new ExecutionList);
        layout->addWidget(m_execution_list->getWidget());

        auto showButton = new QPushButton("Show Tree");
        layout->addWidget(showButton);
        connect(showButton, &QPushButton::clicked, [this]() {

            for (auto execution : m_execution_list->getSelected()) {
                showTraditionalView(execution);
            }

        });

        m_server.reset(new TcpServer([this](intptr_t socketDesc) {

            {
                /// Initiate a receiver thread
                auto receiver = new ReceiverThread(socketDesc, m_settings);
                /// Delete the receiver one the thread is finished
                connect(receiver, &QThread::finished, receiver, &QObject::deleteLater);
                /// Handle the start message in this connector
                connect(receiver, &ReceiverThread::notifyStart, [this, receiver](const std::string& ex_name, int ex_id, bool restarts) {
                    handleStart(receiver, ex_name, ex_id, restarts);
                });

                receiver->start();

            }

        }));

        listen_port_ = DEFAULT_PORT;

        // See if the default port is available
        auto res = m_server->listen(QHostAddress::Any, listen_port_);
        if (!res) {
            // If not, try any port
            m_server->listen(QHostAddress::Any, 0);
            listen_port_ = m_server->serverPort();
        }

        std::cerr << "Ready to listen on: " << listen_port_ << std::endl;

    }

    static int getRandomExID() {
        std::mt19937 rng;
        rng.seed(std::random_device()());
        std::uniform_int_distribution<std::mt19937::result_type> dist(100);

        return dist(rng);
    }

    int Conductor::getNextExecId() const {
        int eid=0;
        while(m_executions.find(eid) != m_executions.end()) {
            eid++;
        }
        return eid;
    }

    void Conductor::setMetaData(int exec_id, const std::string& group_name,
                    const std::string& exec_name,
                    std::shared_ptr<NameMap> nm)
    {
        exec_meta_.insert({exec_id, {group_name, exec_name, nm}});

        debug("force") << "exec_id:" << exec_id << std::endl;
        debug("force") << "gr_name:" << group_name << std::endl;
        debug("force") << "ex_name:" << exec_name << std::endl;
    }

    int Conductor::getListenPort() const {
        return static_cast<int>(listen_port_);
    }

    Conductor::~Conductor(){

        debug("memory") << "~Conductor\n";
    }

    void Conductor::handleStart(ReceiverThread* receiver, const std::string& ex_name, int ex_id, bool restarts) {

        auto res = m_executions.find(ex_id);

        if (res == m_executions.end() || ex_id == 0) {

            /// Note: metadata from MiniZinc IDE overrides that provided by the solver
            std::string ex_name_used = ex_name;

            const bool ide_used = (exec_meta_.find(ex_id) != exec_meta_.end());

            if (ide_used) {
                debug("force") << "already know metadata for this ex_id!\n";
                ex_name_used = exec_meta_[ex_id].ex_name;
            }

            /// needs a new execution
            auto ex = addNewExecution(ex_name_used, ex_id, restarts);

            /// construct a name map
            if (ide_used) {
                ex->setNameMap(exec_meta_[ex_id].name_map);
            } else if (m_options.paths != "" && m_options.mzn != "") {
                auto nm = std::make_shared<NameMap>();
                auto success = nm->initialize(m_options.paths, m_options.mzn);
                if (success) {
                    ex->setNameMap(nm);
                }
            }

            /// The builder should only be created for a new execution
            auto builderThread = new QThread();
            auto builder = new TreeBuilder(*ex);

            m_builders[ex_id] = builder;
            builder->moveToThread(builderThread);

            /// is this the right time to delete the builder thread?
            connect(builderThread, &QThread::finished, builderThread, &QObject::deleteLater);
            builderThread->start();
        }

        /// obtain the builder aready assigned to this execution
        ///(either just now or by another connection)
        auto builder = m_builders[ex_id];

        connect(receiver, &ReceiverThread::newNode,
            builder, &TreeBuilder::handleNode);

        connect(receiver, &ReceiverThread::doneReceiving,
            builder, &TreeBuilder::finishBuilding);


    }

    Execution* Conductor::addNewExecution(const std::string& ex_name, int ex_id, bool restarts) {

        auto ex = std::make_shared<Execution>(ex_name, restarts);

        if (ex_id == 0) {
            ex_id = getRandomExID();
        }

        debug("force") << "EXECUTION_ID: " << ex_id << std::endl;

        m_executions[ex_id] = ex;
        m_execution_list->addExecution(*ex);

        showTraditionalView(ex.get());

        return ex.get();

    }

    ExecutionWindow& Conductor::getExecutionWindow(Execution* e) {
        auto maybe_view = m_execution_windows.find(e);

        /// create new one if doesn't already exist
        if (maybe_view == m_execution_windows.end()) {

            m_execution_windows[e] = utils::make_unique<ExecutionWindow>(*e);
        }

        return *m_execution_windows.at(e);
    }

    void Conductor::mergeTrees(Execution* e1, Execution* e2) {

        /// create new tree

        // QProgressDialog dialog;

        auto window = new analysis::MergeWindow();

        auto& new_tree = window->getTree();
        auto& res = window->mergeResult();

        /// Note: TreeMerger will delete itself when finished
        auto merger = new analysis::TreeMerger(*e1, *e2, new_tree, res);

        connect(merger, &analysis::TreeMerger::finished,
                window, &analysis::MergeWindow::finalize);
        merger->start();

        window->show();

    }

    void Conductor::readSettings() {

        QFile file("settings.json");

        if (!file.exists()) {
            qDebug() << "settings.json not found";
            return;
        }

        file.open(QIODevice::ReadWrite | QIODevice::Text);

        auto data = file.readAll();

        auto json_doc = QJsonDocument::fromJson(data);

        if (json_doc.isEmpty()) {
            qDebug() << "settings.json is empty";
            return;
        }

        if (json_doc.isObject()) {

            auto json_obj = json_doc.object();

            m_settings.receiver_delay = json_obj["receiver_delay"].toInt();
        }

        qDebug() << "settings read";

    }

}


namespace cpprofiler {

    void Conductor::showTraditionalView(Execution* e) {

        auto& window = getExecutionWindow(e);

        window.show();

    }
}