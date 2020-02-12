#include "mainwindow.h"
#include "ui_mainwindow.h"

//sudo apt-get install qtcreator

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    ui->webView->load(QUrl("https://arcc-race.github.io/deepracer-wiki/#/"));
    ui->log->append("Log:\n");
    qDebug() << qApp->applicationDirPath();
    this->refresh();
}

MainWindow::~MainWindow()
{
    //delete all the processes
    delete memory_manager_process;
    delete log_analysis_process;
    delete stop_process;
    delete ui;
}

void MainWindow::closeEvent (QCloseEvent *event)
{
    if(!is_saved){
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Confirmation", "Your DeepRacer settings are not saved! Would you like to save them?",QMessageBox::Yes|QMessageBox::No);
        if(reply == QMessageBox::Yes){
            this->on_save_button_clicked();
        }
    }

    //Cleanup any programs that are still running
//    ui->log->append("Stopping running programs"); //Causes bug do not use
    if(memory_manager_process){
        memory_manager_process->kill();
    }
    if(log_analysis_process){
        log_analysis_process->kill();
    }
    stop_process = new QProcess();
    stop_process->start("/bin/sh", QStringList{stop_script});
    stop_process->waitForFinished();

    event->accept();
}

void MainWindow::refresh(){
    //Update all the text with what is currently in the targeted files
    QFile reward_func_file(reward_func_path);
    if(!reward_func_file.open(QIODevice::ReadOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open reward function file: " + reward_func_file.errorString());
    } else {
        QTextStream in(&reward_func_file);
        current_reward_func = in.readAll();
        reward_func_file.close();
    }

    QFile action_space_file(action_space_path);
    if(!action_space_file.open(QIODevice::ReadOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open action space file: " + action_space_file.errorString());
    } else {
        QTextStream in(&action_space_file);
        current_action_space = in.readAll();
        action_space_file.close();
    }


    QFile hyperparameters_file(hyperparameters_path);
    if(!hyperparameters_file.open(QIODevice::ReadOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open hyperparameters file: " + hyperparameters_file.errorString());
    } else {
        QTextStream in(&hyperparameters_file);
        QString rl_file(in.readAll());
        QString text("Hyperparameters:\n");
        for(int i=0;i<hyperparams.length();i++){
            text += hyperparams[i] + " : ";
            int x=rl_file.indexOf(hyperparams[i])+hyperparams[i].length()+2; //Starts with + 2 because of "": #######," format
            while(rl_file.at(x) != '\n' && rl_file.at(x) != ','  && rl_file.at(x) != '#'){
                text += rl_file.at(x);
                x++;
            }
            text += "\n";
        }
        current_hyperparameters = text;
        //Check if is_using pretrained
        if(rl_file.contains("# \"pretrained")){
            is_pretrained = false;
            ui->log->append("local trainer is NOT using pretrained model");
        } else {
            is_pretrained = true;
            ui->log->append("local trainer is using pretrained model");
        }
        hyperparameters_file.close();
    }


    QFile track_file(track_path);
    if(!track_file.open(QIODevice::ReadOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open track file: " + track_file.errorString());
    } else {
        QTextStream in(&track_file);
        current_track = in.readLine(); //First line contains track
        track_file.close();
    }

    //Set all the text on the GUI to the updated strings
    ui->reward_function->setText(current_reward_func);
    ui->action_space->setText(current_action_space);
    ui->hyper_parameters->setText(current_hyperparameters);
    ui->track_name->setText(current_track);

    is_saved = true;
}

void MainWindow::parse_logfile(){
    //Read log file and update QVector of reward per iteration
    QFile log_file(log_path);
    if(!log_file.open(QIODevice::ReadOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open log file: " + log_file.errorString());
    } else {
        int iteration = 0;
        double reward_per_iteration = 0;
        while(!log_file.atEnd()){
            QString line =log_file.readLine();
            QString marker = "SIM_TRACE_LOG:";
            QStringList list;
            if(line.contains(marker)){
                list = line.right(line.length()-line.indexOf(marker)-marker.length()).split(",");
                if(list[0].toInt() != iteration){
                    reward_per_iteration += list[log_format.indexOf("reward")].toDouble();
                    reward_per_iteration_vector.append(reward_per_iteration);
                    reward_per_iteration = 0;
                    iteration++;
                } else {
                    reward_per_iteration += list[log_format.indexOf("reward")].toDouble();
                }
            }
        }
    }
}

void MainWindow::on_save_button_clicked()
{
    //Save updates writen to the QText editor to the minio bucket and coach_rl python file
    //Update all the text with what is currently in the TextEdit

    QFile reward_func_file(reward_func_path);
    if(!reward_func_file.open(QIODevice::WriteOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open reward function file: " + reward_func_file.errorString());
    } else {
        QTextStream out(&reward_func_file);
        out << ui->reward_function->toPlainText();
        reward_func_file.close();
    }

    QFile action_space_file(action_space_path);
    if(!action_space_file.open(QIODevice::WriteOnly | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open action space file: " + action_space_file.errorString());
    } else {
        QTextStream out(&action_space_file);
        out << ui->action_space->toPlainText();
        action_space_file.close();
    }


    QFile hyperparameters_file(hyperparameters_path);
    if(!hyperparameters_file.open(QIODevice::ReadWrite | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open hyperparameters file: " + hyperparameters_file.errorString());
    } else {
        //Read existing text in the file
        QTextStream in(&hyperparameters_file);
        QString rl_file = in.readAll(); //First line contains track

        //Read the new track from the GUI
        QString text(ui->hyper_parameters->toPlainText());

        //Edit the file text
        for(int i=0;i<hyperparams.length();i++){
            QString re(hyperparams[i]);
            int param_index = rl_file.indexOf(hyperparams[i])+hyperparams[i].length();
            while(rl_file[param_index] != '\n'){
                re+=rl_file[param_index];
                param_index++;
            }
            QString new_value(hyperparams[i]+"\":");
            param_index = text.indexOf(hyperparams[i])+hyperparams[i].length()+3;
            while(text[param_index] != '\n'){
                new_value+=text[param_index];
                param_index++;
            }
            //This makes sure that comma formatting in the hyperparameters python file is correct
            if(hyperparams[i] == "term_cond_avg_score" && is_pretrained == false){
                new_value += "  # place comma here if using pretrained!";
            } else if(hyperparams[i] == "term_cond_avg_score" && is_pretrained == true) {
                new_value += ", # place comma here if using pretrained!";
            } else {
                new_value += ",";
            }
            rl_file.replace(re,new_value);
        }

        //Write edited text back into file
        QTextStream out(&hyperparameters_file);
        hyperparameters_file.resize(0); //clear the existing file
        out << rl_file; //First line contains new track name
        hyperparameters_file.close();
    }


    QFile track_file(track_path);
    if(!track_file.open(QIODevice::ReadWrite | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open track file: " + track_file.errorString());
    } else {
        //Read existing text in the file
        QTextStream in(&track_file);
        QString env_file = in.readAll(); //First line contains track

        //Read the new track from the GUI
        QString replacementText(ui->track_name->text());

        //Edit the file text
        QString first_line = "";
        int i = 0;
        while(env_file[i] !='\n'){
            first_line += env_file[i];
            i++;
        }
        QRegularExpression re(first_line);
        env_file.replace(re, replacementText);

        //Write edited text back into file
        QTextStream out(&track_file);
        track_file.resize(0); //clear the existing file
        out << env_file; //First line contains new track name
        track_file.close();
    }

    ui->log->append("Saved");
    is_saved = true;
}

void MainWindow::on_start_button_clicked()
{
    ui->log->append("Setting training time...");
    int train_time = 3600;
    QDateTime current = QDateTime::currentDateTime();
    QString starttime = current.toString("yyyy/MM/dd HH:mm:ss");
    QString endtime = "";
    QMessageBox::StandardButton reply;
    //Get input for training time
    reply = QMessageBox::question(this, "Confirmation", "Would you like to set training time?",QMessageBox::Yes|QMessageBox::No);
    if(reply == QMessageBox::Yes){
        QStringList items;
        items << tr("5") << tr("10") << tr("30") << tr("60") << tr("90") << tr("120");

        bool ok;
        QString item = QInputDialog::getItem(this, tr("Set Training Time"),
                                            tr("Time(m):"), items, 0, false, &ok);
        if (ok && !item.isEmpty()) {
            bool is_ok;
            train_time = (item.toInt(&is_ok, 10) * 60);
            ui->log->append("Training time is set for " + item + "min.");

            endtime = current.addMSecs(train_time * 1000).toString("yyyy/MM/dd HH:mm:ss");
            ui->log->append("Training start at: " + starttime);
            ui->log->append("Training will end at: " + endtime);
        } else {
            ui->log->append("Training time is set for 60min(default).");
            endtime = current.addMSecs(train_time * 1000).toString("yyyy/MM/dd HH:mm:ss");
            ui->log->append("Training start at: " + starttime);
            ui->log->append("Training will end at: " + endtime);
        }
    }

    //Training will stop according to training time setting
    QTimer::singleShot(train_time * 1000, this, SLOT(timer_stop()));

    ui->log->append("Starting training...");
    //Start the simulation and training instance
    start_process = new QProcess();
    start_process->start("/bin/sh", QStringList{start_script});
    connect(start_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
    [=]  (int exitCode)
    {
        if(exitCode){
            ui->log->append("Failed to start local training");
            delete start_process;
            start_process = nullptr;
        } else {
            ui->log->append("Local training started successfully");
            delete start_process;
            start_process = nullptr;
        }
    });

    //Start the log analysis
    if(!log_analysis_process){
        log_analysis_process = new QProcess();
        log_analysis_process->start("/bin/bash", QStringList{log_analysis_script});
        connect(log_analysis_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        [=]  (int exitCode)
        {
            if(exitCode && !log_analysis_url.contains("http")){
                ui->log->append("log analysis started with an ERROR");
                qDebug() << log_analysis_process->readAllStandardError();
                qDebug() << log_analysis_process->readAllStandardOutput();
                delete log_analysis_process;
                log_analysis_process = nullptr;
            } else {
                ui->log->append("log analysis started correctly");
            }
        });
        //Wait 4 seconds then try to read the URL and update the web widget
        QTimer::singleShot(4000, this, SLOT(update_log_analysis_browser()));
    }

    //Open up a memory manager (needs sudo password from user to actually run)
    if(!memory_manager_process){
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Memory Manager", "Would you like memory manager to clip model and checkpoint memory usage to 3GB?",QMessageBox::Yes|QMessageBox::No);
        if(reply == QMessageBox::Yes){
            ui->log->append("In order to run the memory manager enter your root password!");
            memory_manager_process = new QProcess();
            memory_manager_process->start("pkexec", QStringList{memory_manager_script});
            connect(memory_manager_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=]  (int exitCode)
            {
                if(exitCode){
                    ui->log->append("memory manager failed to start :(");
                    qDebug() << memory_manager_process->readAllStandardError();
                    qDebug() << memory_manager_process->readAllStandardOutput();
                    delete memory_manager_process;
                    memory_manager_process = nullptr;
                } else {
                    ui->log->append("memory memory manager stopped?????");
                    qDebug() << memory_manager_process->readAllStandardError();
                    qDebug() << memory_manager_process->readAllStandardOutput();
                    delete memory_manager_process;
                    memory_manager_process = nullptr;
                }
            });
        }
    }
}

void MainWindow::update_log_analysis_browser()
{
    //If read is ready get parse the URL
    if(log_analysis_process){
        log_analysis_process->open();
        QString log_tool_line = log_analysis_process->readAllStandardError();
        qDebug() << log_tool_line;
        if(log_tool_line.contains('\n')){
            QStringList jupyter_output = log_tool_line.split('\n');
            qDebug() << jupyter_output.length();
            if(jupyter_output.length() < 2){
                ui->log->append("Log analysis not ready. Try refreshing the GUI after a minute to access the log analysis tool!");
             } else {
                log_analysis_url = jupyter_output[jupyter_output.length()-2].replace(" ", "").replace("orhttp", "http");
                qDebug() << "Log analysis URl: " << log_analysis_url;
                log_analysis_process->close();
                if(!log_analysis_url.contains("http")){
                    //QMessageBox::warning(this, "Warning", "Could not read log analysis tool URL, refresh to try again");
                } else {
                    ui->log->append("Log analysis URL loaded: " + log_analysis_url);
                    ui->webView->load(QUrl(log_analysis_url));
                    //Refresh the page to get to the notebook
                    QTimer::singleShot(200, this, SLOT(go_to_notebook()));
                }
            }
        } else {
            ui->log->append("Log analysis not ready. Try refreshing the GUI after a minute to access the log analysis tool!");
        }
    }
}

void MainWindow::go_to_notebook(){
    QString notebook_url = log_analysis_url.left(log_analysis_url.indexOf("\?"));
    notebook_url += "notebooks/Training_analysis.ipynb";
    qDebug() << notebook_url;
    ui->webView->load(QUrl(notebook_url));
}

void MainWindow::on_restart_button_clicked()
{
    //Restart the simulation and training instance using model that has been training (ie using pretrained model)
    //This allows you to tweak the parameters incrementally
    ui->log->append("Restarting...");
    ui->log->append("Stoping last training instance...");
    stop_process = new QProcess();
    stop_process->start("/bin/sh", QStringList{stop_script});
    connect(stop_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
    [=]  (int exitCode)
    {
        if(exitCode){
            ui->log->append("stopped with status ERROR");
            ui->log->append("Restart failed!");
            delete stop_process;
            stop_process = nullptr;
        } else {
            ui->log->append("stopped with status NORMAL");
            delete stop_process;
            stop_process = nullptr;
            use_pretrained_process = new QProcess();
            use_pretrained_process->start("/bin/sh", QStringList{use_pretrained_script});
            connect(use_pretrained_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            [=]  (int exitCode)
            {
                if(exitCode){
                    ui->log->append("pretrained model loaded with status ERROR");
                    ui->log->append("Restart failed!");
                    delete use_pretrained_process;
                    use_pretrained_process = nullptr;
                } else {
                    ui->log->append("pretrained model loaded with status NORMAL");
                    delete use_pretrained_process;
                    use_pretrained_process = nullptr;
                    if(!use_pretrained){
                        this->on_use_pretrained_button_clicked(); //Set rl_coach to use_pretrained model
                    }
                    start_process = new QProcess();
                    start_process->start("/bin/sh", QStringList{start_script});
                    connect(start_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
                    [=]  (int exitCode)
                    {
                        if(exitCode){
                            ui->log->append("restarted trainied with status ERROR");
                            ui->log->append("Restart failed!");
                            delete start_process;
                            start_process = nullptr;
                        } else {
                            ui->log->append("restarted training with status NORMAL");
                            delete start_process;
                            start_process = nullptr;
                        }
                    });
                }
            });
        }
    });
}

void MainWindow::on_stop_button_clicked()
{
    //Stop the training instance
    //Stop the simulation and training instance
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to stop training?",QMessageBox::Yes|QMessageBox::No);
    if(reply == QMessageBox::Yes){
        this->stop_training();
    }
}

void MainWindow::timer_stop(){
    this->stop_training();
}

void MainWindow::stop_training()
{
    ui->log->append("Stopping training...");
    stop_process = new QProcess();
    stop_process->start("/bin/sh", QStringList{stop_script});
    connect(stop_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
    [=]  (int exitCode)
    {
        if(exitCode){
            ui->log->append("training stopped with status ERROR");
            delete stop_process;
            stop_process = nullptr;
        } else {
            ui->log->append("training stopped  with status NORMAL");
            delete stop_process;
            stop_process = nullptr;
        }
    });
}

void MainWindow::on_init_button_clicked()
{
    //Init the Repository, this also can perform recovery if something brakes
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to init local training? All files not saved will be lost!",QMessageBox::Yes|QMessageBox::No);
    if(reply == QMessageBox::Yes){
        init_process = new QProcess();
        init_process->start("/bin/sh", QStringList{init_script});
        connect(init_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        [=]  (int exitCode)
        {
            if(exitCode){
                ui->log->append("init finished with status ERROR");
                qDebug() << init_process->readAllStandardError();
                qDebug() << init_process->readAllStandardOutput();
                delete init_process;
                init_process = nullptr;
            } else {
                ui->log->append("init finished with status NORMAL");
                delete init_process;
                init_process = nullptr;
            }
        });

        ui->log->append("Wait while the init script runs; this may take a minute or two. Once the init script finishes you may run refresh to see reward function, action space, and hyperparameters.");
        ui->log->append("#######There is a known issue with creating the python virtual environment for log analysis in the init script if you have not yet created the venv or jupyter kernel please run:\n `cd aws-deepracer-workshops/log-analysis;virtualenv -p python3 log-analysis.venv;source log-analysis.venv/bin/activate;pip install -r requirements.txt;ipython kernel install --user --name=log-analysis.venv` \nin a terminal from deepracer-for-dummies root driectory. Note that you may need to install virtualenv with `sudo apt-get install virtualenv`#######");
    }
}

void MainWindow::on_uploadbutton_clicked()
{
    //Upload snapshot to S3, make sure envs are set
    QDir dir(pretrained_path);
    if(!dir.exists()){
        QMessageBox::warning(this, "Warning", "No model in pretrained directory! Please save current model as pretrained or load a profile!");
    } else {
        ui->log->append("Uploading model to S3...");
        QString s3_bucket = QInputDialog::getText(this, tr("Uploading to S3"), tr("Name of S3 bucket:"), QLineEdit::Normal);
        QString s3_prefix = QInputDialog::getText(this, tr("Uploading to S3"), tr("S3 prefix:"), QLineEdit::Normal);
        upload_process = new QProcess();
        upload_process->start(upload_script, QStringList{s3_bucket, s3_prefix});
        connect(upload_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        [=]  (int exitCode)
        {
            if(exitCode){
                ui->log->append("upload failed to start");
                delete upload_process;
                upload_process = nullptr;
            } else {
                ui->log->append("upload started successfully, see terminal with upload status");
                delete upload_process;
                upload_process = nullptr;
            }
        });
    }
}

void MainWindow::on_delete_button_clicked()
{
    //Delete last model
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to delete the most recent local model?",QMessageBox::Yes|QMessageBox::No);
    if(reply == QMessageBox::Yes){
        ui->log->append("Deleting last model...");
        delete_process = new QProcess();
        delete_process->start("pkexec", QStringList{delete_script});
        connect(delete_process, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
        [=]  (int exitCode)
        {
            if(exitCode){
                ui->log->append("model deleted with status ERROR");
                delete delete_process;
                delete_process = nullptr;
            } else {
                ui->log->append("model deleted with status NORMAL");
                delete delete_process;
                delete_process = nullptr;
            }
        });
    }
}

void MainWindow::on_refresh_button_clicked()
{
    QMessageBox::StandardButton reply;
    if(!is_saved){
        reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to refresh the GUI? Not all changes have been saved!",QMessageBox::Yes|QMessageBox::No);
        if(reply == QMessageBox::Yes){
            this->refresh();
            ui->log->append("GUI refreshed.");
        } else {
            ui->log->append("GUI refresh aborted.");
        }
    } else {
        this->refresh();
        ui->log->append("GUI refreshed.");
    }
    update_log_analysis_browser();
}

void MainWindow::on_use_pretrained_button_clicked()
{
    //Use pretrained lines are in the hyperparameters file
    QFile hyperparameters_file(hyperparameters_path);
    if(!hyperparameters_file.open(QIODevice::ReadWrite | QFile::Text)){
        QMessageBox::warning(this, "Warning", "Cannot open hyperparameters file: " + hyperparameters_file.errorString());
    } else {
        QTextStream in(&hyperparameters_file);
        QString hyperparameters_pretrained = in.readAll();
        int pretrained_bucket_index = hyperparameters_pretrained.indexOf("pretrained_s3_bucket")-3;
        int pretrained_prefix_index = hyperparameters_pretrained.indexOf("pretrained_s3_prefix")-3;
        int pretrained_comma_index = hyperparameters_pretrained.indexOf("# place comma here")-2;
        if(!is_pretrained){
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to turn ON use_pretrained? Make sure you have saved all configurations before proceeding!",QMessageBox::Yes|QMessageBox::No);
            if(reply == QMessageBox::Yes){
                hyperparameters_pretrained[pretrained_bucket_index] = ' ';
                hyperparameters_pretrained[pretrained_prefix_index] = ' ';
                hyperparameters_pretrained[pretrained_comma_index] = ',';
                is_pretrained = true;
                ui->log->append("local trainer set to use pretrained model");
                //Write edited text back into file
                QTextStream out(&hyperparameters_file);
                hyperparameters_file.resize(0); //clear the existing file
                out << hyperparameters_pretrained;
            }
        } else {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Confirmation", "Are you sure you want turn OFF use_pretrained? Make sure you have saved all configurations before proceeding!",QMessageBox::Yes|QMessageBox::No);
            if(reply == QMessageBox::Yes){
                hyperparameters_pretrained[pretrained_bucket_index] = '#';
                hyperparameters_pretrained[pretrained_prefix_index] = '#';
                hyperparameters_pretrained[pretrained_comma_index] = ' ';
                is_pretrained = false;
                ui->log->append("local trainer will not use pretrained model");
                //Write edited text back into file
                QTextStream out(&hyperparameters_file);
                hyperparameters_file.resize(0); //clear the existing file
                out << hyperparameters_pretrained;
            }
        }
    }
    hyperparameters_file.close();
}

void MainWindow::on_reward_function_textChanged()
{
    is_saved = false;
}

void MainWindow::on_action_space_textChanged()
{
    is_saved = false;
}

void MainWindow::on_track_name_textChanged()
{
    is_saved = false;
}

void MainWindow::on_hyper_parameters_textChanged()
{
    is_saved = false;
}

void MainWindow::on_actionSave_as_Profile_triggered()
{
    // Open a file for reading
    QFile profiles_file(profiles_path);
    if(!profiles_file.open(QIODevice::ReadWrite | QIODevice::Text))
    {
        QMessageBox::warning(this, "Warning", "Cannot open profiles file: " + profiles_file.errorString());
    } else {
        if(!profiles_xml.setContent(&profiles_file))
        {
            QMessageBox::warning(this, "Warning", "Could not read profile xml file");
        } else {
            QDomElement root = profiles_xml.firstChildElement();
            QString profile_name = QInputDialog::getText(this, tr("Profile Naming"), tr("Profile name:"), QLineEdit::Normal);
            QDomElement profile = profiles_xml.createElement(profile_name);
            profile.setAttribute("Date", QDateTime::currentDateTime().toString());

            //Access log file for updating the graph and log-analysis tools
            log_path = QFileDialog::getOpenFileName(this,"Open the most recently created log file for this profile.");
            ui->log->append("Reading " + log_path);
            this->parse_logfile(); //Get the reward values from the log file. This will help determine the best model in the profile

            //Find the iteration with the max reward and the max reward and add it to the profile
            double max_reward = -999999999;
            int max_index = -1;
            for(int i=0;i<reward_per_iteration_vector.length();i++){
                if(reward_per_iteration_vector[i] > max_reward){
                    max_reward = reward_per_iteration_vector[i];
                    max_index = i;
                }
            }
            profile.setAttribute("MaxReward", QString::number(max_reward));
            profile.setAttribute("MaxIteration", QString::number(max_index));
            profile.setAttribute("LogPath", log_path);
            profile.setAttribute("Hyperparameters", current_hyperparameters);
            profile.setAttribute("Track", current_track);
            profile.setAttribute("RewardFunction", current_reward_func);
            profile.setAttribute("ActionSpace", current_action_space);
            root.appendChild(profile);

            QTextStream stream(&profiles_file);
            profiles_file.resize(0);
            stream << profiles_xml.toString();

            //move model into profiles with correct name
            if(!this->cpDir("../docker/volumes/minio/bucket/rl-deepracer-sagemaker", "../profiles/" + profile_name)){
                QMessageBox::warning(this, "Warning", "Error copying model to profiles");
            }
            //copy the log file over
            QFile::copy(log_path, "../profiles/" + profile_name + "/" + profile_name + ".log");
            qDebug() << log_path;

        }
    }
    profiles_file.close();
}

bool MainWindow::cpDir(const QString &srcPath, const QString &dstPath)
{
    QDir parentDstDir(QFileInfo(dstPath).path());
    if (!parentDstDir.mkdir(QFileInfo(dstPath).fileName()))
        return false;

    QDir srcDir(srcPath);
    foreach(const QFileInfo &info, srcDir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
        QString srcItemPath = srcPath + "/" + info.fileName();
        QString dstItemPath = dstPath + "/" + info.fileName();
        if (info.isDir()) {
            if (!cpDir(srcItemPath, dstItemPath)) {
                return false;
            }
        } else if (info.isFile()) {
            if (!QFile::copy(srcItemPath, dstItemPath)) {
                return false;
            }
        } else {
            qDebug() << "Unhandled item" << info.filePath() << "in cpDir";
        }
    }
    return true;
}

void MainWindow::on_actionLoad_Profile_triggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirmation", "Are you sure you want to load a new profile? This will delete your current pretrained model and reset your training files",QMessageBox::Yes|QMessageBox::No);
    if(reply == QMessageBox::Yes){
        //Fist copy the model into pretrained directory
        QString profile_name = QInputDialog::getText(this, tr("Profile Loading"), tr("Name of profile to load:"), QLineEdit::Normal);
        QDir profile_dir("../profiles/" + profile_name);
        if(profile_dir.exists()){
            QDir pretrained_dir(pretrained_path);
            if(pretrained_dir.exists()){
                pretrained_dir.removeRecursively(); //make sure the that directory is empty
            }
            if(!this->cpDir("../profiles/" + profile_name, pretrained_path)){
                QMessageBox::warning(this, "Warning", "Error copying profile to pretrained");
            }
            //Copy log file into log dir for analysis (May not be working)
            QFile::copy( "../profiles/" + profile_name + "/" + profile_name + ".log", "../docker/volumes/robo/checkpoint/log/" + profile_name + ".log");
            //Now update GUI and trainig file with info in the XML
            QFile profiles_file(profiles_path);
            if(!profiles_file.open(QIODevice::ReadWrite | QIODevice::Text))
            {
                QMessageBox::warning(this, "Warning", "Cannot open profiles file: " + profiles_file.errorString());
            } else {
                if(!profiles_xml.setContent(&profiles_file))
                {
                    QMessageBox::warning(this, "Warning", "Could not read profile xml file");
                } else {
                    QDomElement root=profiles_xml.documentElement();
                    QDomElement profile = root.lastChildElement(profile_name); //last = most recent
                    current_hyperparameters = profile.attribute("Hyperparameters");
                    current_track = profile.attribute("Track");
                    current_reward_func = profile.attribute("RewardFunction");
                    current_action_space = profile.attribute("ActionSpace");
                    //Set all the text on the GUI to the updated strings
                    ui->reward_function->setText(current_reward_func);
                    ui->action_space->setText(current_action_space);
                    ui->hyper_parameters->setText(current_hyperparameters);
                    ui->track_name->setText(current_track);
                    ui->log->append("Log path for loaded profile: " + profile.attribute("LogPath") +
                                    " If the log file has been deleted, manually copy the log file (profile_name.log) in the respective profiles directory to the docker log directory.");
                    ui->log->append("Iteration with max reward for loaded profile: " + profile.attribute("MaxIteration"));
                    ui->log->append("Max reward for loaded profile: " + profile.attribute("MaxReward"));
                }
            }
            profiles_file.close();
        } else {
            QMessageBox::warning(this, "Warning", "Could not find profile: " + profile_name);
        }
    }
}
