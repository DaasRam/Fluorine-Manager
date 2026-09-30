#include <QCoreApplication>
#include <gtest/gtest.h>
#include <uibase/log.h>

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    MOBase::log::LoggerConfiguration configuration;
    configuration.name = "python-runner-tests";
    MOBase::log::createDefault(configuration);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
