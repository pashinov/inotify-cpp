#include <inotify-cpp/NotifierBuilder.h>

#include <boost/filesystem/fstream.hpp>
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <fstream>
#include <future>
#include <iostream>

using namespace inotify;

void openFile(boost::filesystem::path file)
{
    std::ifstream stream;
    stream.open(file.string(), std::ifstream::in);
    BOOST_CHECK(stream.is_open());
    stream.close();
}

struct NotifierBuilderTests {
    NotifierBuilderTests()
        : testDirectory_("testDirectory")
        , recursiveTestDirectory_(testDirectory_ / "recursiveTestDirectory")
        , testFile_(testDirectory_ / "test.txt")
        , timeout_(1)
    {
        boost::filesystem::create_directories(testDirectory_);
        boost::filesystem::ofstream stream(testFile_);
    }
    ~NotifierBuilderTests() = default;

    boost::filesystem::path testDirectory_;
    boost::filesystem::path recursiveTestDirectory_;
    boost::filesystem::path testFile_;

    std::chrono::seconds timeout_;

    // Events
    std::promise<Notification> promisedOpen_;
    std::promise<Notification> promisedCloseNoWrite_;
};

BOOST_FIXTURE_TEST_CASE(shouldNotAcceptNotExistingPaths, NotifierBuilderTests)
{
    BOOST_CHECK_THROW(
        BuildNotifier().watchPathRecursively("/not/existing/path/"), std::invalid_argument);
    BOOST_CHECK_THROW(BuildNotifier().watchFile("/not/existing/file"), std::invalid_argument);
}

BOOST_FIXTURE_TEST_CASE(shouldNotifyOnOpenEvent, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_).onEvent(
        Event::open, [&](Notification notification) { promisedOpen_.set_value(notification); });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFile_);

    auto futureOpenEvent = promisedOpen_.get_future();
    BOOST_CHECK(futureOpenEvent.wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(futureOpenEvent.get().event == Event::open);
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldNotifyOnMultipleEvents, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_).onEvents(
        { Event::open, Event::close_nowrite }, [&](Notification notification) {
            switch (notification.event) {
            case Event::open:
                promisedOpen_.set_value(notification);
                break;
            case Event::close_nowrite:
                promisedCloseNoWrite_.set_value(notification);
                break;
            }
        });

    std::thread thread([&notifier]() {
        notifier.runOnce();
        notifier.runOnce();
    });

    openFile(testFile_);

    auto futureOpen = promisedOpen_.get_future();
    auto futureCloseNoWrite = promisedCloseNoWrite_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(futureOpen.get().event == Event::open);
    BOOST_CHECK(futureCloseNoWrite.wait_for(timeout_) == std::future_status::ready);
    BOOST_CHECK(futureCloseNoWrite.get().event == Event::close_nowrite);
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldStopRunOnce, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_);

    std::thread thread([&notifier]() { notifier.runOnce(); });

    notifier.stop();

    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldStopRun, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_);

    std::thread thread([&notifier]() { notifier.run(); });

    notifier.stop();

    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldIgnoreFileOnce, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_).ignoreFileOnce(testFile_).onEvent(
        Event::open, [&](Notification notification) { promisedOpen_.set_value(notification); });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFile_);

    auto futureOpen = promisedOpen_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) != std::future_status::ready);

    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldIgnoreFile, NotifierBuilderTests)
{
    auto notifier = BuildNotifier().watchFile(testFile_).ignoreFile(testFile_).onEvent(
        Event::open, [&](Notification notification) { promisedOpen_.set_value(notification); });

    std::thread thread([&notifier]() { notifier.run(); });

    openFile(testFile_);
    openFile(testFile_);

    auto futureOpen = promisedOpen_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) != std::future_status::ready);

    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldWatchPathRecursively, NotifierBuilderTests)
{

    auto notifier = BuildNotifier()
                        .watchPathRecursively(testDirectory_)
                        .onEvent(Event::open, [&](Notification notification) {
                            switch (notification.event) {
                            case Event::open:
                                promisedOpen_.set_value(notification);
                                break;
                            }

                        });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFile_);

    auto futureOpen = promisedOpen_.get_future();
    BOOST_CHECK(futureOpen.wait_for(timeout_) == std::future_status::ready);

    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldUnwatchPath, NotifierBuilderTests)
{
    std::promise<Notification> timeoutObserved;
    std::chrono::milliseconds timeout(100);

    auto notifier = BuildNotifier().watchFile(testFile_).unwatchFile(testFile_);

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFile_);
    BOOST_CHECK(promisedOpen_.get_future().wait_for(timeout_) != std::future_status::ready);
    notifier.stop();
    thread.join();
}

BOOST_FIXTURE_TEST_CASE(shouldCallUserDefinedUnexpectedExceptionObserver, NotifierBuilderTests)
{
    std::promise<void> observerCalled;

    auto notifier = BuildNotifier().watchFile(testFile_).onUnexpectedEvent(
        [&](Notification) { observerCalled.set_value(); });

    std::thread thread([&notifier]() { notifier.runOnce(); });

    openFile(testFile_);

    BOOST_CHECK(observerCalled.get_future().wait_for(timeout_) == std::future_status::ready);
    thread.join();
}
