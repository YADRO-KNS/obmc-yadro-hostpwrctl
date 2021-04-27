/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2021 YADRO.
 */

#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/exception.hpp>
#include <sdeventplus/clock.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include <sdeventplus/source/event.hpp>
#include <sdeventplus/utility/timer.hpp>

#include <cstdio>

constexpr auto clockId = sdeventplus::ClockId::RealTime;
using Timer = sdeventplus::utility::Timer<clockId>;
constexpr auto confirmationTime = 30;

static sdbusplus::bus::bus systemBus = sdbusplus::bus::new_default();
static sdeventplus::Event systemEvent = sdeventplus::Event::get_default();

constexpr auto chassisPath = "/xyz/openbmc_project/state/chassis0";
constexpr auto chassisIface = "xyz.openbmc_project.State.Chassis";
constexpr auto chassisState = "CurrentPowerState";
constexpr auto chassisStateOn =
    "xyz.openbmc_project.State.Chassis.PowerState.On";
constexpr auto chassisStateOff =
    "xyz.openbmc_project.State.Chassis.PowerState.Off";
constexpr auto chassisTransition = "RequestedPowerTransition";
constexpr auto chassisTransitionOff =
    "xyz.openbmc_project.State.Chassis.Transition.Off";

constexpr auto hostPath = "/xyz/openbmc_project/state/host0";
constexpr auto hostIface = "xyz.openbmc_project.State.Host";
constexpr auto hostState = "CurrentHostState";
constexpr auto hostStateOn = "xyz.openbmc_project.State.Host.HostState.Running";
constexpr auto hostStateOff = "xyz.openbmc_project.State.Host.HostState.Off";
constexpr auto hostTransition = "RequestedHostTransition";
constexpr auto hostTransitionOn =
    "xyz.openbmc_project.State.Host.Transition.On";
constexpr auto hostTransitionOff =
    "xyz.openbmc_project.State.Host.Transition.Off";
constexpr auto hostTransitionReboot =
    "xyz.openbmc_project.State.Host.Transition.Reboot";

constexpr auto ifaceDBusProperties = "org.freedesktop.DBus.Properties";

static std::string currentChassisState, expectedChassisState;
static std::string currentHostState, expectedHostState;

/**
 * @brief Remove class name form the property value
 *        For example 'xyz.foo.bar.value' -> 'value'
 *
 * @param value - Original value
 *
 * @return trimmed value
 */
inline std::string trimClassName(const std::string& value)
{
    auto last = value.rfind('.');
    if (last && last != std::string::npos)
    {
        return value.substr(last + 1);
    }
    return value;
}

/**
 * @brief Get D-Bus service name
 *
 * @param path  - object path
 * @param iface - D-Bus interface
 *
 * @return D-Bus service name
 */
static std::string getService(const std::string& path, const std::string& iface)
{
    auto method = systemBus.new_method_call(
        "xyz.openbmc_project.ObjectMapper",
        "/xyz/openbmc_project/object_mapper",
        "xyz.openbmc_project.ObjectMapper", "GetObject");

    std::vector<std::string> ifaces = {iface};
    method.append(path, ifaces);

    std::map<std::string, std::vector<std::string>> services;
    try
    {
        systemBus.call(method).read(services);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        fprintf(stderr, "Error occurred during the object mapper call: %s\n",
                e.what());
    }

    if (!services.empty())
    {
        return std::move(services.begin()->first);
    }

    return std::string();
}

/**
 * @brief Get D-Bus property value
 *
 * @param path     - object path
 * @param iface    - D-Bus interface
 * @param property - property name
 *
 * @return property value
 */
std::string getProperty(const std::string& path, const std::string& iface,
                        const std::string& property)
{
    auto service = getService(path, iface);
    if (service.empty())
    {
        return std::string();
    }

    auto method = systemBus.new_method_call(service.c_str(), path.c_str(),
                                            ifaceDBusProperties, "Get");
    method.append(iface, property);

    try
    {
        std::variant<std::string> data;
        systemBus.call(method).read(data);
        return std::move(std::get<std::string>(data));
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        fprintf(stderr, "Error occurred during get property request, %s\n",
                e.what());
    }

    return "";
}

/**
 * @brief Set D-Bus property
 *
 * @param path     - object path
 * @param iface    - D-Bus interface
 * @param property - property name
 * @param value    - new value
 */
void setProperty(const std::string& path, const std::string& iface,
                 const std::string& property, const std::string& value)
{
    auto service = getService(path, iface);
    if (service.empty())
    {
        return;
    }

    auto method = systemBus.new_method_call(service.c_str(), path.c_str(),
                                            ifaceDBusProperties, "Set");
    std::variant<std::string> data(value);
    method.append(iface, property, data);

    try
    {
        systemBus.call(method);
    }
    catch (const sdbusplus::exception::SdBusError& e)
    {
        fprintf(stderr, "Error occurred during set property request, %s\n",
                e.what());
    }
}

/**
 * @brief Terminate the event loop if expected values reached
 */
void exitOnExpectedState()
{
    if (!expectedHostState.empty() && !expectedChassisState.empty() &&
        expectedHostState == currentHostState &&
        expectedChassisState == currentChassisState)
    {
        systemEvent.exit(EXIT_SUCCESS);
    }
}

/**
 * @brief PropertiesChanged signal handler
 *
 * @param m - signal data
 */
void onPropertiesChanged(sdbusplus::message::message& m)
{
    std::string iface;
    std::map<std::string, std::variant<std::string>> data;
    std::vector<std::string> v;

    m.read(iface, data, v);

    if (iface == chassisIface)
    {
        auto it = data.find(chassisState);
        if (it != data.end())
        {
            currentChassisState = std::get<std::string>(it->second);
            printf("Current Chassis State: %s\n",
                   trimClassName(currentChassisState).c_str());
            exitOnExpectedState();
        }
    }
    else if (iface == hostIface)
    {
        auto it = data.find(hostState);
        if (it != data.end())
        {
            currentHostState = std::get<std::string>(it->second);
            printf("Current Host State: %s\n",
                   trimClassName(currentHostState).c_str());
            exitOnExpectedState();
        }
    }
}

/**
 * @brief Send the power on command
 */
void switchHostPowerOn(sdeventplus::source::EventBase&)
{
    if (currentChassisState != chassisStateOn)
    {
        expectedHostState = hostStateOn;
        expectedChassisState = chassisStateOn;
        setProperty(hostPath, hostIface, hostTransition, hostTransitionOn);
        printf("Power up signal was sent to host, waiting for system start.\n");
    }
    else
    {
        printf("System is already up.\n");
        systemEvent.exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Send the gracefully shut down command
 */
void switchHostPowerOff(sdeventplus::source::EventBase&)
{
    if (currentChassisState != chassisStateOff)
    {
        expectedHostState = hostStateOff;
        expectedChassisState = chassisStateOff;
        setProperty(hostPath, hostIface, hostTransition, hostTransitionOff);
        printf("Shutdown signal was sent to host, waiting for system down.\n");
    }
    else
    {
        printf("System is already down.\n");
        systemEvent.exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Send the forced shut down command
 */
void switchChassisPowerOff(sdeventplus::source::EventBase&)
{
    if (currentChassisState != chassisStateOff)
    {
        expectedHostState = hostStateOff;
        expectedChassisState = chassisStateOff;
        setProperty(chassisPath, chassisIface, chassisTransition,
                    chassisTransitionOff);
        printf(
            "Shutdown signal was sent to chassis, waiting for system down.\n");
    }
    else
    {
        printf("System is already down.\n");
        systemEvent.exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Reset the host power
 */
void resetHostPower(sdeventplus::source::EventBase&)
{
    if (currentChassisState != chassisStateOff)
    {
        expectedHostState = hostStateOn;
        expectedChassisState = chassisStateOn;
        setProperty(hostPath, hostIface, hostTransition, hostTransitionReboot);
        printf("Reboot signal was sent to host, waiting for system down and "
               "start again.\n");
    }
    else
    {
        printf("Chassis is off, reboot is impossible.\n");
        systemEvent.exit(EXIT_SUCCESS);
    }
}

/**
 * @brief Show actual power state
 */
void showPowerStatus(sdeventplus::source::EventBase& source)
{
    printf("Current Chassis state: %s\n",
           trimClassName(currentChassisState).c_str());
    printf("Current Host state: %s\n", trimClassName(currentHostState).c_str());

    source.get_event().exit(0);
}

/**
 * @brief Convert the command name to the action
 *
 * @param command - command name
 *
 * @return function to execute
 */
sdeventplus::source::EventBase::Callback getAction(const char* command)
{
    if (0 == strcmp(command, "on"))
    {
        return switchHostPowerOn;
    }
    if (0 == strcmp(command, "off"))
    {
        return switchChassisPowerOff;
    }
    if (0 == strcmp(command, "soft"))
    {
        return switchHostPowerOff;
    }
    if (0 == strcmp(command, "reboot"))
    {
        return resetHostPower;
    }
    if (0 == strcmp(command, "status"))
    {
        return showPowerStatus;
    }

    return nullptr;
}

/**
 * @brief Show help message
 *
 * @param app - application name
 */
void showUsage(const char* app)
{
    printf("Usage: %s <command>\n", app);
    printf(R"(The commands:
  on     - turn the host on
  off    - turn the host off
  soft   - gracefully turn the host off
  reset  - resetting host power
  status - show actual host power state
)");
}

/**
 * @brief Application entry point
 */
int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        showUsage(argv[0]);
        return EXIT_FAILURE;
    }

    auto action = getAction(argv[1]);
    if (!action)
    {
        showUsage(argv[0]);
        return EXIT_FAILURE;
    }

    systemBus.attach_event(systemEvent.get(), SD_EVENT_PRIORITY_NORMAL);

    sdbusplus::bus::match::match hostStateMatch(
        systemBus,
        sdbusplus::bus::match::rules::propertiesChanged(hostPath, hostIface),
        std::bind(onPropertiesChanged, std::placeholders::_1));

    sdbusplus::bus::match::match chassisStateMatch(
        systemBus,
        sdbusplus::bus::match::rules::propertiesChanged(chassisPath,
                                                        chassisIface),
        std::bind(onPropertiesChanged, std::placeholders::_1));

    sdeventplus::source::Defer defer(systemEvent, std::move(action));

    Timer timer{systemEvent,
                [](Timer&) {
                    printf("Unable to confirm operation success "
                           "within timeout period (%d s).\n",
                           confirmationTime);
                    systemEvent.exit(EXIT_FAILURE);
                },
                std::chrono::seconds(confirmationTime)};

    currentChassisState = getProperty(chassisPath, chassisIface, chassisState);
    currentHostState = getProperty(hostPath, hostIface, hostState);

    return systemEvent.loop();
}
