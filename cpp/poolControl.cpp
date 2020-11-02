#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <unistd.h>


enum class Command {
    NONE,
    AllOff,
    Antifreeze,
    Heat,
    BubblesToggle,
    Increment,
    Decrement,
    ShutdownSystem
};


class PoolControl
{
public:
    void AllOff() { circulation = false; heaterActive = false; heaterEnabled = false; bubbles = false; setOutputs(); }
    void AntiFreeze() { circulation = true; heaterEnabled = true; heaterActive = false; if (tSet > 10) tSet = 5; setOutputs(); }
    void Heat() { circulation = true; heaterEnabled = true; heaterActive = false; if (tSet < 15) tSet = 39; setOutputs(); }
    void ToggleBubbles() { bubbles = !bubbles; setOutputs(); }
    void Increment() { tSet += 1 * (tSet < 42); tSet = std::floor(tSet); };
    void Decrement() { tSet -= 1 * (tSet > 5); tSet = std::floor(tSet); };
    
    void ShutdownRPI()
    {
        AllOff();
        storeRestartSettings();
        sleep(3);
        system("sudo shutdown -h now");
    }

    std::string getStateString()
    {
        // tempSet, tempCurrent, tempAmbient, stateWaterlevel, stateCirculation, stateHeater, stateBubbles
        std::ostringstream oss;
        oss << tSet << ";"
            << tNow << ";"
            << tAmbient << ";"
            << 1 * waterLevelOK << ";"
            << 1 * circulation << ";"
            << 1 * heaterEnabled + 2 * heaterActive << ";"
            << 1 * bubbles;
        return oss.str();
    }

    void process()
    {
        fetchWaterLevel();
        fetchCurrentTemp();
        fetchAmbientTemp();

        if (!waterLevelOK)
        {
            circulation = false;
            heaterActive = false;
            heaterEnabled = false;
        }

        if (tNow - tSet > tHysteresis || !circulation || !heaterEnabled)
        {
            heaterActive = false;
        }

        if (circulation && heaterEnabled && tNow - tSet < -tHysteresis)
        {
            heaterActive = true;
        }

        setOutputs();
    }

    void storeRestartSettings()
    {
        std::ofstream ofs("/home/pi/poolfiles/restartSettings");
        ofs << circulation << "\n"
            << heaterEnabled << "\n"
            << tSet;
    }

    PoolControl()
    {
        sleep(25); // wait for system to finish booting
        {
            // Setup GPIO
            std::ofstream gpioExport("/sys/class/gpio/export");
            gpioExport
                << "17" << std::endl    // 11 water level input
               // << "27" << std::endl    // 13   Extra heater (future)
                << "22" << std::endl    // 15   Heater
                << "23" << std::endl    // 16   Water pump
                << "24" << std::endl;   // 18   Air pump
        }
        sleep(3); // wait and set directions
        std::ofstream("/sys/class/gpio/gpio17/direction") << "in" << std::flush;
        //std::ofstream("/sys/class/gpio/gpio27/direction") << "out" << std::flush;
        std::ofstream("/sys/class/gpio/gpio22/direction") << "out" << std::flush;
        std::ofstream("/sys/class/gpio/gpio23/direction") << "out" << std::flush;
        std::ofstream("/sys/class/gpio/gpio24/direction") << "out" << std::flush;

        // Reload startup settings
        std::ifstream ifs("/home/pi/poolfiles/restartSettings");
        ifs >> circulation;
        ifs >> heaterEnabled;
        ifs >> tSet;
        tSet = std::round(tSet);
        if (tSet < 5) tSet = 5;
        if (tSet > 42) tSet = 42;
    }

private:
    const double tHysteresis = 0.3;
    double tSet = 38;
    double tNow = 22;
    double tAmbient = 0;
    
    bool waterLevelOK = false;
    bool circulation = false;
    bool heaterEnabled = false;
    bool heaterActive = false;
    bool bubbles = false;

    void fetchCurrentTemp()
    {
        std::ifstream ifs("/sys/bus/w1/devices/28-000004af27e4/temperature");
        int temp1000 = 9900;
        ifs >> temp1000;
        tNow = std::round(temp1000 / 100.0)/10.0;

    }
    void fetchAmbientTemp()
    {
        std::ifstream ifs("/sys/bus/w1/devices/28-00000494e126/temperature");
        int temp1000 = 0;
        if (ifs.is_open())
        {
            ifs >> temp1000;
        }
        tAmbient = std::round(temp1000 / 100.0) / 10.0;
    }

    void fetchWaterLevel()
    {
        std::ifstream ifs("/sys/class/gpio/gpio17/value");
        int waterLevelVal = 0;
        ifs >> waterLevelVal;
        waterLevelOK = (waterLevelVal == 1);
    }

    void setOutputs()
    {
        std::ofstream("/sys/class/gpio/gpio22/value") << (heaterActive ? "1" : "0") << std::flush;
        std::ofstream("/sys/class/gpio/gpio23/value") << (circulation ? "1" : "0") << std::flush;
        std::ofstream("/sys/class/gpio/gpio24/value") << (bubbles ? "1" : "0") << std::flush;
    }
};

void resetCommandFile()
{
    std::ofstream oStr("/home/pi/poolfiles/poolCmd");
    oStr << 'n';
}

Command getCommand()
{
    char ctrlCh = 'n';
    Command cmd = Command::NONE;
    {
        std::ifstream iStr("/home/pi/poolfiles/poolCmd");
        iStr >> ctrlCh;
    }

    if (ctrlCh != 'n')
    {
        switch (ctrlCh)
        {
        case 'o':
            cmd = Command::AllOff;
            break;
        case 'f':
            cmd = Command::Antifreeze;
            break;
        case 'h':
            cmd = Command::Heat;
            break;
        case 'b':
            cmd = Command::BubblesToggle;
            break;
        case 'i':
            cmd = Command::Increment;
            break;
        case 'd':
            cmd = Command::Decrement;
            break;
        case 'q':
            cmd = Command::ShutdownSystem;
            break;
        default:
            break;
        }

        resetCommandFile();
    }

    return cmd;
}

void writeStateString(const std::string& stateStr)
{
    std::ofstream ofs("/home/pi/poolfiles/poolState");
    ofs << stateStr;
}

int main()
{
    PoolControl pc;
    usleep(1000000);
    resetCommandFile();
    std::string oldState;
    unsigned int roundsSinceLastCommand = 0;
    unsigned int roundsUntilNextTempCheck = 0;
    for (;;)
    {
        do
        {
            ++roundsSinceLastCommand;
            Command cmd = getCommand();
            switch (cmd)
            {
            case Command::BubblesToggle: pc.ToggleBubbles(); break;
            case Command::AllOff: pc.AllOff(); break;
            case Command::Antifreeze: pc.AntiFreeze(); break;
            case Command::Heat: pc.Heat(); break;
            case Command::Increment: pc.Increment(); break;
            case Command::Decrement: pc.Decrement(); break;
            case Command::ShutdownSystem: pc.ShutdownRPI(); break;
            }

            if (cmd != Command::NONE)
            {
                roundsSinceLastCommand = 0;
                roundsUntilNextTempCheck = 0; // force process new data
            }

            std::string newState = pc.getStateString();
            if (newState != oldState)
            {
                writeStateString(newState);
                pc.storeRestartSettings();
                oldState = newState;
            }

            usleep(100000); // 0.1 seconds
            //std::cout << roundsSinceLastCommand << ",  " << std::flush;
        } while (roundsSinceLastCommand < 10); // Be a bit more responsive 1 seconds after each command is received (avoid reading temperature and so)

        if (roundsUntilNextTempCheck == 0)
        {
            pc.process();
            roundsUntilNextTempCheck = 4*10; // normally wait ten seconds between each temp check.
        }
        --roundsUntilNextTempCheck;

        usleep(250000); // 0.25 seconds
    }

    return 0;
}