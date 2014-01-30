// Copyright (C) 2014 Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

#ifndef IFACE_MGR_TEST_CONFIG_H
#define IFACE_MGR_TEST_CONFIG_H

#include <asiolink/io_address.h>
#include <dhcp/iface_mgr.h>
#include <boost/noncopyable.hpp>

namespace isc {
namespace dhcp {
namespace test {

///
/// @name Set of structures describing interface flags.
///
//@{
/// @brief Structure describing the loopback interface flag.
struct FlagLoopback {
    explicit FlagLoopback(bool flag) : flag_(flag) { }
    bool flag_;
};

/// @brief Structure describing the up interface flag.
struct FlagUp {
    explicit FlagUp(bool flag) : flag_(flag) { }
    bool flag_;
};

/// @brief Structure describing the running interface flag.
struct FlagRunning {
    explicit FlagRunning(bool flag) : flag_(flag) { }
    bool flag_;
};

/// @brief Structure describing the inactive4 interface flag.
struct FlagInactive4 {
    explicit FlagInactive4(bool flag) : flag_(flag) { }
    bool flag_;
};

/// @brief Structure describing the inactive6 interface flag.
struct FlagInactive6 {
    explicit FlagInactive6(bool flag) : flag_(flag) { }
    bool flag_;
};
//@}

/// @brief Convenience class for configuring @c IfaceMgr for unit testing.
///
/// This class is used by various unit tests which test the code relaying
/// on IfaceMgr. The use of this class is not limited to libdhcp++ validation.
/// There are other libraries and applications (e.g. DHCP servers) which
/// depend on @c IfaceMgr.
///
/// During the normal operation, the @c IfaceMgr detects interfaces present
/// on the machine where it is running. It also provides the means for
/// applications to open sockets on these interfaces and perform other
/// IO operations. This however creates dependency of the applications
/// using @c IfaceMgr on the physical properties of the system and effectively
/// makes it very hard to unit test the dependent code.
///
/// Unit tests usually require that @c IfaceMgr holds a list of well known
/// interfaces with the well known set of IP addresses and other properties
/// (a.k.a. interface flags). The solution which works for many test scenarios
/// is to provide a set of well known fake interfaces, by bypassing the
/// standard interface detection procedure and manually adding @c Iface objects
/// which encapsulate the fake interfaces. As a consequence, it becomes
/// impossible to test IO operations (e.g. sending packets) because real sockets
/// can't be opened on these interfaces. The @c PktFilterTestStub class
/// is used by this class to mimic behavior of IO operations on fake sockets.
///
/// This class provides a set of convenience functions that should be called
/// by unit tests to configure the @c IfaceMgr with fake interfaces.
class IfaceMgrTestConfig : public boost::noncopyable {
public:

    /// @brief Constructor.
    ///
    /// It closes all sockets opened by @c IfaceMgr and removes all interfaces
    /// being used by @c IfaceMgr.
    IfaceMgrTestConfig(const bool default_config = false);

    /// @brief Destructor.
    ///
    /// Closes all currently opened sockets, removes current interfaces and
    /// sets the default packet filtering classes. The default packet filtering
    /// classes are used for IO operations on real sockets/interfaces.
    ///
    /// Destructor also re-detects real interfaces.
    ~IfaceMgrTestConfig();

    /// @brief Adds new IPv4 or IPv6 address to the interface.
    ///
    /// @param iface_name Name of the interface on which new address should
    /// be configured.
    /// @param IPv4 or IPv6 address to be configured on the interface.
    void addAddress(const std::string& iface_name,
                    const asiolink::IOAddress& address);

    /// @brief Configures new interface for the @c IfaceMgr.
    ///
    /// @param iface Object encapsulating interface to be added.
    void addIface(const Iface& iface);

    /// @brief Configures new interface for the @c IfaceMgr.
    ///
    /// @param name Name of the new interface.
    /// @param ifindex Index for a new interface.
    void addIface(const std::string& name, const int ifindex);

    /// @brief Create an object representing interface.
    ///
    /// Apart from creating an interface, this function also sets the
    /// interface flags:
    /// - loopback flag if interface name is "lo"
    /// - up always true
    /// - running always true
    /// - inactive always to false
    /// - multicast always to true
    /// - broadcast always to false
    ///
    /// If one needs to modify the default flag settings, the setIfaceFlags
    /// function should be used.
    ///
    /// @param name A name of the interface to be created.
    /// @param ifindex An index of the interface to be created.
    ///
    /// @return An object representing interface.
    static Iface createIface(const std::string& name, const int ifindex);

    /// @brief Creates a default (example) set of fake interfaces.
    void createIfaces();

    /// @brief Sets various flags on the specified interface.
    ///
    /// This function configures interface with new values for flags.
    ///
    /// @param loopback Specifies if interface is a loopback interface.
    /// @param up Specifies if the interface is up.
    /// @param running Specifies if the interface is running.
    /// @param inactive4 Specifies if the interface is inactive for V4
    /// traffic, i.e. @c IfaceMgr opens V4 sockets on this interface.
    /// @param inactive6 Specifies if the interface is inactive for V6
    /// traffic, i.e. @c IfaceMgr opens V6 sockets on this interface.
    void setIfaceFlags(const std::string& name,
                       const FlagLoopback& loopback,
                       const FlagUp& up,
                       const FlagRunning& running,
                       const FlagInactive4& inactive4,
                       const FlagInactive6& inactive6);

};

};
};
};

#endif // IFACE_MGR_TEST_CONFIG_H
