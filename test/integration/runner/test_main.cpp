// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_application.h>
#include <test/integration/fixture/test_process.h>
#include <test/integration/runner/test_cases.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaMethod>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {
struct RunnerOptions {
    const IntegrationTestModule* module{nullptr};
    std::optional<WalletMode> wallet_mode;
    QString profile;
    QString profile_dir;
    QString selector;
    bool discover{false};
    bool list_functions{false};
    bool list_modules{false};
    bool help{false};
};

struct IntegrationCase {
    const IntegrationTestModule* module{nullptr};
    WalletMode mode{WalletMode::Disabled};
    QString selector{};
    std::optional<IntegrationTestResult> result{};
};

QStringList TestFunctions(const QObject& tests)
{
    QStringList functions;
    const auto* meta = tests.metaObject();
    for (int i{0}; i < meta->methodCount(); ++i) {
        const auto method = meta->method(i);
        const auto name = method.name();
        if (method.access() != QMetaMethod::Private || method.methodType() != QMetaMethod::Slot ||
            method.parameterCount() != 0 || method.returnType() != QMetaType::Void ||
            name.endsWith("_data") || name == "initTestCase" || name == "cleanupTestCase" ||
            name == "init" || name == "cleanup") continue;
        functions.push_back(QString::fromUtf8(name));
    }
    return functions;
}

std::vector<IntegrationCase> TestCases(const IntegrationTestModule* selected_module,
                                     std::optional<WalletMode> wallet_mode, const QString& filter,
                                     const QString& profile_dir, bool list_functions = false)
{
    std::vector<IntegrationCase> cases;
    BitcoinQmlApplication app;
    for (const auto& module : IntegrationTestModules()) {
        if (selected_module && &module != selected_module) continue;
        for (const auto mode : module.wallet_modes) {
            if (wallet_mode && mode != *wallet_mode) continue;
            // Constructors and data functions only describe tests. Discovery
            // must not start an application or initialize a node.
            const auto tests = module.create(app, mode);
            QStringList functions;
            for (const auto& function : TestFunctions(*tests)) {
                if (filter.isEmpty() || function == filter.section(':', 0, 0)) functions.push_back(function);
            }
            if (functions.empty()) continue;
            if (list_functions) {
                for (const auto& function : functions) cases.push_back({&module, mode, function});
                continue;
            }
            auto discovery = DiscoverIntegrationTests(module, mode, QString::fromLatin1(tests->metaObject()->className()), functions, profile_dir);
            if (discovery.result.status == IntegrationTestStatus::Failed) {
                cases.push_back({&module, mode, {}, std::move(discovery.result)});
                continue;
            }
            for (const auto& selector : discovery.tests) {
                if (filter.contains(':') && selector != filter) continue;
                cases.push_back({&module, mode, selector});
            }
        }
    }
    return cases;
}

QString CaseName(const IntegrationCase& test)
{
    if (!test.module) return "selection";
    return QString::fromUtf8(test.module->name) + "." + WalletModeName(test.mode) + "." +
           (test.selector.isEmpty() ? "discovery" : test.selector);
}

QStringList ChildOptions(const QStringList& arguments)
{
    QStringList options;
    for (int i{1}; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (option == "-o") {
            ++i;
        } else if (option == "-silent" || option == "-v1" || option == "-v2" || option == "-vs" || option == "-nocrashhandler") {
            options.push_back(option);
        } else if (option == "-eventdelay" || option == "-keydelay" || option == "-mousedelay" || option == "-maxwarnings") {
            options.push_back(option);
            if (++i < arguments.size()) options.push_back(arguments[i]);
        }
    }
    return options;
}

bool PrepareReports(const QStringList& arguments)
{
    const QStringList formats{"txt", "csv", "xml", "lightxml", "junitxml", "xunitxml", "teamcity", "tap"};
    QStringList filenames;
    bool modern{false};
    bool legacy{false};
    int legacy_files{0};
    for (int i{1}; i < arguments.size(); ++i) {
        if (formats.contains(arguments[i].mid(1)) && arguments[i].startsWith('-')) legacy = true;
        if (arguments[i] != "-o") continue;
        if (++i == arguments.size() || arguments[i].isEmpty()) {
            std::cerr << "Missing Qt Test output filename\n";
            return false;
        }
        const auto& output = arguments[i];
        const bool formatted = output.contains(',');
        modern |= formatted;
        legacy |= !formatted;
        legacy_files += !formatted;
        if (formatted && !formats.contains(output.section(',', 1))) {
            std::cerr << "Invalid Qt Test output format; use -o filename,format\n";
            return false;
        }
        filenames.push_back(output.section(',', 0, 0));
    }
    if ((modern && legacy) || legacy_files > 1) {
        std::cerr << "Use -o filename,format for every output; do not mix it with legacy -o filename or format flags\n";
        return false;
    }
    QSet<QString> paths;
    for (const auto& filename : filenames) {
        const auto path = filename == "-" ? filename : QDir::cleanPath(QFileInfo{filename}.absoluteFilePath());
        if (filename.isEmpty() || paths.contains(path)) {
            std::cerr << "Each Qt Test output needs a distinct, nonempty filename\n";
            return false;
        }
        paths.insert(path);
    }
    for (const auto& filename : filenames) {
        if (filename == "-") continue;
        if (!QFileInfo{filename}.dir().mkpath(".")) {
            std::cerr << "Cannot prepare Qt Test report directory\n";
            return false;
        }
    }
    return true;
}
} // namespace

class IntegrationTests : public QObject
{
    Q_OBJECT
    std::vector<IntegrationCase> m_cases;
    const RunnerOptions m_options;
    const QStringList m_child_options;

public:
    IntegrationTests(RunnerOptions options, QStringList child_options)
        : m_options{std::move(options)}, m_child_options{std::move(child_options)} {}

private Q_SLOTS:
    void run_data()
    {
        QTest::addColumn<int>("index");
        m_cases = TestCases(m_options.module, m_options.wallet_mode, m_options.selector, m_options.profile_dir);
        if (m_cases.empty()) {
            m_cases.push_back({nullptr, WalletMode::Disabled, {},
                               IntegrationTestResult{IntegrationTestStatus::Failed, "No tests match the selected module, wallet mode, and test function/data row"}});
        }
        for (int i{0}; i < static_cast<int>(m_cases.size()); ++i) {
            const auto& test = m_cases[i];
            QTest::newRow(qPrintable(CaseName(test))) << i;
        }
    }

    void run()
    {
        QFETCH(int, index);
        const auto& test = m_cases.at(index);
        const auto result = test.result ? *test.result : RunIntegrationTest(*test.module, test.mode, test.selector, m_options.profile_dir, m_child_options);
        ReportIntegrationMessages(result);
        const auto file = result.source_file.toUtf8();
        const auto* source = file.isEmpty() ? __FILE__ : file.constData();
        const int line = result.source_line > 0 ? result.source_line : __LINE__;
        if (result.status == IntegrationTestStatus::Failed) {
            QTest::qFail(qPrintable(result.details), source, line);
        } else if (result.status == IntegrationTestStatus::Skipped) {
            QTest::qSkip(qPrintable(result.details), source, line);
        }
    }
};

namespace {
std::optional<RunnerOptions> ParseOptions(int& argc, char* argv[])
{
    RunnerOptions options;
    int output_index{1};
    for (int i{1}; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "-o") {
            argv[output_index++] = argv[i];
            if (i + 1 < argc) argv[output_index++] = argv[++i];
        } else if (argument == "--help" || argument == "--list-modules") {
            options.help = argument == "--help";
            options.list_modules = true;
            return options;
        } else if (argument == "-functions") {
            options.list_functions = true;
        } else if (argument == "--test-discover") {
            options.discover = true;
        } else if (argument.starts_with("--module=")) {
            const auto name = argument.substr(std::string_view{"--module="}.size());
            if (options.module) {
                std::cerr << "Specify --module once.\n";
                return std::nullopt;
            }
            options.module = FindIntegrationTestModule(name);
            if (!options.module) {
                std::cerr << "Unknown integration module: " << name << '\n';
                return std::nullopt;
            }
        } else if (argument.starts_with("--wallet=")) {
            const auto value = argument.substr(std::string_view{"--wallet="}.size());
            if (options.wallet_mode || (value != "disabled" && value != "enabled")) {
                std::cerr << "Specify --wallet=disabled or --wallet=enabled once.\n";
                return std::nullopt;
            }
            options.wallet_mode = value == "enabled" ? WalletMode::Enabled : WalletMode::Disabled;
        } else if (argument.starts_with("--test=")) {
            if (!options.selector.isEmpty() || argument == "--test=") {
                std::cerr << "Specify a nonempty --test once.\n";
                return std::nullopt;
            }
            options.selector = QString::fromUtf8(argv[i] + std::string_view{"--test="}.size());
        } else if (argument.starts_with("--profile-dir=")) {
            if (!options.profile_dir.isEmpty() || argument == "--profile-dir=") {
                std::cerr << "Specify a nonempty --profile-dir once.\n";
                return std::nullopt;
            }
            options.profile_dir = QString::fromUtf8(argv[i] + std::string_view{"--profile-dir="}.size());
        } else if (argument.starts_with("--test-profile=")) {
            if (!options.profile.isEmpty() || argument == "--test-profile=") {
                std::cerr << "Specify a nonempty --test-profile once.\n";
                return std::nullopt;
            }
            options.profile = QString::fromUtf8(argv[i] + std::string_view{"--test-profile="}.size());
        } else {
            argv[output_index++] = argv[i];
        }
    }
    argc = output_index;
    argv[argc] = nullptr;
    if (options.module && options.wallet_mode &&
        std::find(options.module->wallet_modes.begin(), options.module->wallet_modes.end(), *options.wallet_mode) == options.module->wallet_modes.end()) {
        std::cerr << "Module " << options.module->name << " does not support wallets " << WalletModeName(*options.wallet_mode) << '\n';
        return std::nullopt;
    }
    return options;
}

void PrintModules(bool help)
{
    if (help) {
        std::cout << "Run one integration test suite with isolated application processes.\n"
                     "Options: --module=NAME --wallet=disabled|enabled --list-modules\n"
                     "         --test=FUNCTION[:ROW] (select a function or data row)\n"
                     "         --profile-dir=PATH (retain failed profiles here)\n"
                     "Omitted options select all modules and their supported wallet modes.\n"
                     "Qt Test options control the combined output and reports.\n"
                     "Use -functions to list functions and -datatags to list individual rows.\n";
    }
    for (const auto& entry : IntegrationTestModules()) {
        std::cout << entry.name << ':';
        for (const auto mode : entry.wallet_modes) std::cout << ' ' << WalletModeName(mode);
        std::cout << '\n';
    }
}

int RunParent(int argc, char* argv[], const RunnerOptions& options)
{
    if (options.discover) return EXIT_FAILURE;
    QCoreApplication app(argc, argv);
    if (!PrepareReports(app.arguments())) return EXIT_FAILURE;
    if (options.list_functions) {
        const auto cases = TestCases(options.module, options.wallet_mode, options.selector, options.profile_dir, true);
        for (const auto& test : cases) std::cout << CaseName(test).toStdString() << "()\n";
        return cases.empty() ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    IntegrationTests tests{options, ChildOptions(app.arguments())};
    return QTest::qExec(&tests, argc, argv);
}

int RunDiscovery(int argc, char* argv[], const RunnerOptions& options)
{
    // Qt's -datatags exits the process, so it must run before constructing
    // the application fixture or starting a node.
    QCoreApplication core(argc, argv);
    BitcoinQmlApplication application;
    const auto tests = options.module->create(application, *options.wallet_mode);
    return QTest::qExec(tests.get(), QStringList{QString::fromUtf8(argv[0]), "-datatags"});
}

int RunChild(int argc, char* argv[], const RunnerOptions& options)
{
    QStringList arguments;
    for (int i{0}; i < argc; ++i) arguments.push_back(QString::fromUtf8(argv[i]));
    const auto child_options = ChildOptions(arguments);
    if (!options.module || !options.wallet_mode || (!options.discover && options.selector.isEmpty()) ||
        child_options != arguments.mid(1) || options.list_functions) {
        std::cerr << "A child requires a module, wallet mode, and a test or discovery request\n";
        return EXIT_FAILURE;
    }
    const auto profile = QDir{options.profile}.absolutePath();
    if (!PrepareIntegrationChild(profile, options.discover)) return EXIT_FAILURE;
    if (options.discover) return RunDiscovery(argc, argv, options);

    TestApplication app{*options.wallet_mode, profile};
    return app.runTests(argv[0], {"-qml_onboarded=1"}, [&](BitcoinQmlApplication& application) {
        const auto tests = options.module->create(application, *options.wallet_mode);
        return QTest::qExec(tests.get(), QStringList{QString::fromUtf8(argv[0]), options.selector, "-o", "qt-results.xml,xml"} + child_options);
    });
}
} // namespace

int main(int argc, char* argv[])
{
    const auto options = ParseOptions(argc, argv);
    if (!options) return EXIT_FAILURE;
    if (options->list_modules) {
        PrintModules(options->help);
        return EXIT_SUCCESS;
    }
    if (options->profile.isEmpty()) return RunParent(argc, argv, *options);
    return RunChild(argc, argv, *options);
}

#include <test_main.moc>
