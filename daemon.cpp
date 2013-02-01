#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>

#include "daemon.h"
#include "iniparser.h"

using namespace std;

#define LOG(fd,x,...)     if (fd) { \
                            time_t now; \
                            time(&now); \
                            char timeNow[30]; \
                            strftime(timeNow, 30, "%Y-%m-%d %X", localtime(&now)); \
                            fprintf(fd, "[%s] "x"\n", timeNow, ##__VA_ARGS__); \
                            fflush(fd); \
                          } \

/**
 * Konstruktorius
 *
 * @param string configFile - kelias iki konfigo (nebutinai pilnas, nes jis skaitomas pries daemonizavima)
 * @param string daemonName - demono pavadinimas
 */
Daemon::Daemon(string configFile, string daemonName)
{
    _configFile = configFile;
    _daemon = daemonName;
    _pidFile = string("/tmp/") + daemonName + ".pid";
    _logFile = string("/tmp/overlord.log");
    _debug = true;
    _ini = NULL;

    if (!configFile.empty()) {
        _ini = iniparser_load( (char*)configFile.c_str());

        if (_ini) {
            _logFile = iniparser_getstring(_ini, (char*)"logging:logfile", (char*)_logFile.c_str());
            _pidFile = iniparser_getstring(_ini, (char*)(daemonName+".main:pidfile").c_str(), (char*)_pidFile.c_str());
        }
        else {
            LOG(stderr, "Can not parse ini: %s\n", configFile.c_str());
        }
    }
}


/**
 * Destruktorius
 *
 */
Daemon::~Daemon()
{
    if (_ini)
        iniparser_freedict(_ini);
}


/**
 * Grazinama int reiksme is konfigo. `var` formatas - 'sekcija:kintamasis'
 *
 * @param string var
 * @param int defVal - default reiksme
 * @return int
 */
int Daemon::configGetInt(std::string var, int defVal = 0)
{
    if (_ini)
        return iniparser_getint(_ini, (char*)var.c_str(), defVal);
}


/**
 * Grazinama string reiksme is konfigo. `var` formatas - 'sekcija:kintamasis'
 *
 * @param string var
 * @param string defVal - default reiksme
 * @return string
 */
std::string Daemon::configGetString(std::string var, string defVal = "")
{
    if (_ini)
        return iniparser_getstring(_ini, (char*)var.c_str(), (char*)defVal.c_str());
}


/**
 * Grazinama bool reiksme is konfigo. `var` formatas - 'sekcija:kintamasis'
 *
 * @param string var
 * @param bool defVal - default reiksme
 * @return bool
 */
bool Daemon::configGetBool(std::string var, bool defVal)
{
    if (_ini)
        return iniparser_getboolean(_ini, (char*)var.c_str(), defVal);
}


/**
 * Grazinama double reiksme is konfigo. `var` formatas - 'sekcija:kintamasis'
 *
 * @param string var
 * @param double defVal - default reiksme
 * @return double
 */
double Daemon::configGetDouble(std::string var, double defVal)
{
    if (_ini)
        return iniparser_getdouble(_ini, (char*)var.c_str(), defVal);
}


/**
 * Demonizavimo implementacija
 *
 */
void Daemon::daemonize()
{
    //log("(DEBUG) Starting daemonize()");

    pid_t pid, sid;
    pid = fork();

    // first fork
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    else if (pid < 0) {
        LOG(stderr, "fork #1 failed! rc = %d", pid);
        exit(EXIT_FAILURE);
    }

    // decouple from parent environment
    umask(0);
    sid = setsid();

    // do second fork
    pid = fork();
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    else if (pid < 0){
        LOG(stderr, "fork #2 failed!");
        exit(EXIT_FAILURE);
    }

    if ((chdir("/")) < 0) {
        LOG(stderr, "::chdir('/') failed!");
        exit(EXIT_FAILURE);
    }

    ofstream file(_pidFile.c_str());

    if (file.is_open()) {
        file << getpid();
        file.close();
    } else {
        _log(LogLevelError, "Couldn't write pid to %s", _pidFile.c_str());
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}


/**
 * .pid failo trynimas
 *
 */
void Daemon::delpid()
{
    if (remove(_pidFile.c_str()) != 0) {
        _log(LogLevelWarning, "Can not remove pidfile '%s'", _pidFile.c_str());
    }
}


/**
 * Demono startavimas
 *
 */
void Daemon::start()
{
    ifstream fd(_pidFile.c_str());
    if (fd.is_open()) {
        LOG(stderr, "pidfile %s already exists. Daemon already running?", _pidFile.c_str());
        exit(EXIT_FAILURE);
    }

    _log(LogLevelInfo, string("-- Starting %s .. --------"), _daemon.c_str());
    _log(LogLevelInfo, string("-- Using %s"), _configFile.c_str());

    daemonize();
    run();
}


/**
 * Demono stabdymas
 *
 */
void Daemon::stop()
{
    ifstream fd(_pidFile.c_str());
    if (!fd.is_open()) {
        LOG(stderr, "pidfile %s doesnt not exist. Daemon not running?", _pidFile.c_str());
        exit(EXIT_FAILURE);
    }

    pid_t pid = 0;
    fd >> pid;
    fd.close();

    while (true) {
        int rc = kill(pid, SIGTERM);
        if (rc != 0) {
            delpid();
            break;
        }
        usleep(100);
    }
}


/**
 * Demono perkrovimas
 *
 */
void Daemon::restart()
{
    stop();
    start();
}


/**
 * Loginimas i faila. Galima naudoti printf() stiliaus argumentus
 *
 * @param int logLevel - pranesimo tipas (LogLevelDebug, LogLevelError, LogLevelWarning, LogLevelInfo)
 * @param string text - tekstas su printf() formatu
 */
void Daemon::_log(int logLevel, std::string text, ...)
{
    if (logLevel == LogLevelDebug && !_debug)
        return;

    _logFileFd = fopen(_logFile.c_str(), "a+");
    if (_logFileFd) {
        FILE *fd = _logFileFd;
        time_t now;
        time(&now);
        char timeNow[30];
        strftime(timeNow, 30, "[%Y-%m-%d %X] ", localtime(&now));

        string logLevelStr;
        switch (logLevel) {
        case LogLevelDebug:
            logLevelStr = "(DEBUG) ";
            break;
        case LogLevelError:
            logLevelStr = "(ERROR) ";
            break;
        case LogLevelWarning:
            logLevelStr = "(WARNING) ";
            break;
        case LogLevelInfo:
            logLevelStr = "(INFO) ";
            break;
        default:
            logLevelStr = "(DEBUG) ";
        }

        text = string(timeNow) + logLevelStr + text + "\n";

        va_list args;
        va_start(args, text.c_str());
        va_end(args);

        vfprintf(fd, text.c_str(), args);
        fflush(fd);
    }

    fclose(_logFileFd);
}
