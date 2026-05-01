#include <Poco/Util/ServerApplication.h>
#include <Poco/Util/Option.h>
#include <Poco/Util/OptionSet.h>
#include <Poco/Util/HelpFormatter.h>
#include <Poco/Net/HTTPServer.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Logger.h>
#include <Poco/ConsoleChannel.h>
#include <Poco/AutoPtr.h>
#include "protocol_probe/api/Handlers.hpp"
#include "protocol_probe/Config.hpp"
#include "protocol_probe/CliRunner.hpp"
#include <sstream>
#include <optional>

using namespace Poco::Util;
using namespace Poco::Net;
using namespace protocol_probe;

class FtdiUnifiedApp : public ServerApplication {
public:
    FtdiUnifiedApp()
        : _showHelp(false), _verbose(false), _mode("server")
        , _cmd("status"), _payload("")
    {}

protected:
    void initialize(Application& self) override {
        Poco::AutoPtr<Poco::ConsoleChannel> pCons(new Poco::ConsoleChannel);
        Poco::Logger::root().setChannel(pCons);
        ServerApplication::initialize(self);

        try {
            loadConfiguration();
        } catch (const Poco::Exception&) {
            Poco::Logger::get("main").warning(
                "protocol_probe.properties not found - using built-in defaults.");
        }

        ProbeConfig::load(config());
    }

    void defineOptions(OptionSet& options) override {
        ServerApplication::defineOptions(options);

        options.addOption(Option("help", "h", "display help")
            .required(false)
            .repeatable(false)
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("verbose", "v", "enable verbose logging")
            .required(false)
            .repeatable(false)
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("mode", "", "server or cli mode")
            .required(false)
            .repeatable(false)
            .argument("mode")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("cmd", "", "CLI command")
            .required(false)
            .repeatable(false)
            .argument("command")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("payload", "", "hex payload")
            .required(false)
            .repeatable(false)
            .argument("hex")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("duration", "", "duration in ms")
            .required(false)
            .repeatable(false)
            .argument("ms")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("baud", "", "UART baud rate")
            .required(false)
            .repeatable(false)
            .argument("baud")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("address", "", "address/start register")
            .required(false)
            .repeatable(false)
            .argument("value")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("length", "", "length/size")
            .required(false)
            .repeatable(false)
            .argument("value")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("slave", "", "slave/device id")
            .required(false)
            .repeatable(false)
            .argument("id")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("regs", "", "register count")
            .required(false)
            .repeatable(false)
            .argument("count")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("port", "p", "HTTP server port")
            .required(false)
            .repeatable(false)
            .argument("port")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));

        options.addOption(Option("tx", "", "UART TX pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("rx", "", "UART RX pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("mosi", "", "SPI MOSI pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("miso", "", "SPI MISO pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("sck", "", "SPI SCK pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("cs", "", "SPI CS pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("scl", "", "I2C SCL pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
        options.addOption(Option("sda", "", "I2C SDA pin")
            .required(false)
            .repeatable(false)
            .argument("pin")
            .callback(OptionCallback<FtdiUnifiedApp>(this, &FtdiUnifiedApp::handleOption)));
    }

    void handleOption(const std::string& name, const std::string& value) {
        if (name == "help") {
            _showHelp = true;
            stopOptionsProcessing();
        } else if (name == "verbose") {
            _verbose = true;
        } else if (name == "mode") {
            _mode = value;
        } else if (name == "cmd") {
            _cmd = value;
        } else if (name == "payload") {
            _payload = value;
        } else if (name == "duration") {
            _durationOverride = std::stoi(value);
        } else if (name == "baud") {
            _baudOverride = static_cast<uint32_t>(std::stoul(value, nullptr, 0));
        } else if (name == "address") {
            _addressOverride = static_cast<uint32_t>(std::stoul(value, nullptr, 0));
        } else if (name == "length") {
            _lengthOverride = static_cast<size_t>(std::stoul(value, nullptr, 0));
        } else if (name == "slave") {
            _slaveIdOverride = static_cast<uint8_t>(std::stoul(value, nullptr, 0));
        } else if (name == "regs") {
            _regCountOverride = static_cast<uint16_t>(std::stoul(value, nullptr, 0));
        } else if (name == "port") {
            _portOverride = static_cast<unsigned short>(std::stoul(value, nullptr, 0));
        } else if (name == "tx") {
            _pinTxOverride = ProbeConfig::pinIndex(value);
        } else if (name == "rx") {
            _pinRxOverride = ProbeConfig::pinIndex(value);
        } else if (name == "mosi") {
            _pinMosiOverride = ProbeConfig::pinIndex(value);
        } else if (name == "miso") {
            _pinMisoOverride = ProbeConfig::pinIndex(value);
        } else if (name == "sck") {
            _pinSckOverride = ProbeConfig::pinIndex(value);
        } else if (name == "cs") {
            _pinCsOverride = ProbeConfig::pinIndex(value);
        } else if (name == "scl") {
            _pinSclOverride = ProbeConfig::pinIndex(value);
        } else if (name == "sda") {
            _pinSdaOverride = ProbeConfig::pinIndex(value);
        }
    }

    int main(const std::vector<std::string>&) override {
        if (_showHelp) {
            displayHelp();
            return Application::EXIT_OK;
        }

        if (_verbose) {
            Poco::Logger::root().setLevel("debug");
        }

        if (_mode == "cli") {
            return runCli();
        }
        return runServer();
    }

private:
    int runServer() {
        unsigned short port = _portOverride.value_or(ProbeConfig::serverPort());
        Poco::Net::ServerSocket svs(port);
        Poco::Net::HTTPServer srv(
            new protocol_probe::api::RequestHandlerFactory,
            svs,
            new Poco::Net::HTTPServerParams);

        Poco::Logger::get("main").information("HTTP server listening on port " + std::to_string(port));
        srv.start();
        waitForTerminationRequest();
        srv.stop();
        return Application::EXIT_OK;
    }

    int runCli() {
        int duration = _durationOverride.value_or(ProbeConfig::discoveryDefaultDuration());
        uint32_t baud = _baudOverride.value_or(ProbeConfig::uartDefaultBaud());
        uint32_t address = _addressOverride.value_or(static_cast<uint32_t>(ProbeConfig::modbusDefaultStart()));
        size_t length = _lengthOverride.value_or(static_cast<size_t>(ProbeConfig::cliDefaultLength()));
        uint8_t slaveId = _slaveIdOverride.value_or(ProbeConfig::cliDefaultSlaveId());
        uint16_t regCount = _regCountOverride.value_or(static_cast<uint16_t>(ProbeConfig::modbusDefaultRegisterCount()));
        uint8_t pinTx = _pinTxOverride.value_or(ProbeConfig::uartTxPin());
        uint8_t pinRx = _pinRxOverride.value_or(ProbeConfig::uartRxPin());
        uint8_t pinMosi = _pinMosiOverride.value_or(ProbeConfig::spiMosiPin());
        uint8_t pinMiso = _pinMisoOverride.value_or(ProbeConfig::spiMisoPin());
        uint8_t pinSck = _pinSckOverride.value_or(ProbeConfig::spiSckPin());
        uint8_t pinCs = _pinCsOverride.value_or(ProbeConfig::spiCsPin());
        uint8_t pinScl = _pinSclOverride.value_or(ProbeConfig::i2cSclPin());
        uint8_t pinSda = _pinSdaOverride.value_or(ProbeConfig::i2cSdaPin());

        CliRunner runner(
            _cmd,
            _payload,
            duration,
            baud,
            address,
            length,
            slaveId,
            regCount,
            pinTx,
            pinRx,
            pinMosi,
            pinMiso,
            pinSck,
            pinCs,
            pinScl,
            pinSda,
            [this]() { this->displayHelp(); }
        );
        return runner.run();
    }

    void displayHelp() {
        HelpFormatter hf(options());
        hf.setCommand(commandName());
        hf.setUsage("[options]");
        hf.setHeader("FT232RL Protocol Probe");
        hf.setFooter("Use --mode=cli with --cmd to run command-line probes.");
        std::ostringstream ss;
        hf.format(ss);
        std::istringstream lines(ss.str());
        std::string line;
        auto& logger = Poco::Logger::get("main");
        while (std::getline(lines, line)) {
            if (!line.empty()) {
                logger.information(line);
            }
        }
    }

private:
    bool _showHelp;
    bool _verbose;
    std::string _mode;
    std::string _cmd;
    std::string _payload;
    std::optional<unsigned short> _portOverride;
    std::optional<int> _durationOverride;
    std::optional<uint32_t> _baudOverride;
    std::optional<uint32_t> _addressOverride;
    std::optional<size_t> _lengthOverride;
    std::optional<uint8_t> _slaveIdOverride;
    std::optional<uint16_t> _regCountOverride;
    std::optional<uint8_t> _pinTxOverride;
    std::optional<uint8_t> _pinRxOverride;
    std::optional<uint8_t> _pinMosiOverride;
    std::optional<uint8_t> _pinMisoOverride;
    std::optional<uint8_t> _pinSckOverride;
    std::optional<uint8_t> _pinCsOverride;
    std::optional<uint8_t> _pinSclOverride;
    std::optional<uint8_t> _pinSdaOverride;
};

int main(int argc, char** argv) {
    FtdiUnifiedApp app;
    return app.run(argc, argv);
}
