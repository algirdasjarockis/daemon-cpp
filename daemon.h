#ifndef __DAEMON_H
#define __DAEMON_H

#include <iostream>
#include <fstream>
#include "iniparser.h"

class Daemon
{
private:
    bool _debug;
    std::string _daemon;
    std::string _configFile;
    std::string _logFile;
    std::string _pidFile;
    dictionary *_ini;
    FILE * _logFileFd;

protected:
    enum {
        LogLevelDebug,
        LogLevelError,
        LogLevelWarning,
        LogLevelInfo
    };

    void _log(int logLevel, std::string text, ...);
    int configGetInt(std::string var, int defVal);
    std::string configGetString(std::string var, std::string defVal);
    bool configGetBool(std::string var, bool defVal);
    double configGetDouble(std::string var, double defVal);
public:
    Daemon(std::string configFile, std::string daemonName);
    ~Daemon();

    void daemonize();
    void delpid();
    void start();
    void stop();
    void restart();
    virtual void run() = 0;
};
