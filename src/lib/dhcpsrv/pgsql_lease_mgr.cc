// Copyright (C) 2014-2019 Internet Systems Consortium, Inc. ("ISC")
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <config.h>

#include <asiolink/io_address.h>
#include <dhcp/duid.h>
#include <dhcp/hwaddr.h>
#include <dhcpsrv/dhcpsrv_log.h>
#include <dhcpsrv/dhcpsrv_exceptions.h>
#include <dhcpsrv/pgsql_lease_mgr.h>

#include <boost/static_assert.hpp>

#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <time.h>

using namespace isc;
using namespace isc::asiolink;
using namespace isc::db;
using namespace isc::dhcp;
using namespace isc::data;
using namespace std;

namespace {

/// @brief Catalog of all the SQL statements currently supported.  Note
/// that the order columns appear in statement body must match the order they
/// that the occur in the table.  This does not apply to the where clause.
PgSqlTaggedStatement tagged_statements[] = {
    // DELETE_LEASE4
    { 1, { OID_INT8 },
      "delete_lease4",
      "DELETE FROM lease4 WHERE address = $1"},

    // DELETE_LEASE4_STATE_EXPIRED
    { 2, { OID_INT8, OID_TIMESTAMP },
      "delete_lease4_state_expired",
      "DELETE FROM lease4 "
          "WHERE state = $1 AND expire < $2"},

    // DELETE_LEASE6
    { 1, { OID_VARCHAR },
      "delete_lease6",
      "DELETE FROM lease6 WHERE address = $1"},

    // DELETE_LEASE6_STATE_EXPIRED
    { 2, { OID_INT8, OID_TIMESTAMP },
      "delete_lease6_state_expired",
      "DELETE FROM lease6 "
          "WHERE state = $1 AND expire < $2"},

    // GET_LEASE4
    { 0, { OID_NONE },
      "get_lease4",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4"},

    // GET_LEASE4_ADDR
    { 1, { OID_INT8 },
      "get_lease4_addr",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE address = $1"},

    // GET_LEASE4_CLIENTID
    { 1, { OID_BYTEA },
      "get_lease4_clientid",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE client_id = $1"},

    // GET_LEASE4_CLIENTID_SUBID
    { 2, { OID_BYTEA, OID_INT8 },
      "get_lease4_clientid_subid",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE client_id = $1 AND subnet_id = $2"},

    // GET_LEASE4_HWADDR
    { 1, { OID_BYTEA },
      "get_lease4_hwaddr",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE hwaddr = $1"},

    // GET_LEASE4_HWADDR_SUBID
    { 2, { OID_BYTEA, OID_INT8 },
      "get_lease4_hwaddr_subid",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE hwaddr = $1 AND subnet_id = $2"},

    // GET_LEASE4_PAGE
    { 2, { OID_INT8, OID_INT8 },
      "get_lease4_page",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE address > $1 "
      "ORDER BY address "
      "LIMIT $2"},

    // GET_LEASE4_SUBID
    { 1, { OID_INT8 },
      "get_lease4_subid",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
      "state, user_context "
      "FROM lease4 "
      "WHERE subnet_id = $1"},

    // GET_LEASE4_EXPIRE
    { 3, { OID_INT8, OID_TIMESTAMP, OID_INT8 },
      "get_lease4_expire",
      "SELECT address, hwaddr, client_id, "
        "valid_lifetime, extract(epoch from expire)::bigint, subnet_id, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context "
      "FROM lease4 "
      "WHERE state != $1 AND expire < $2 AND valid_lifetime != 4294967295 "
      "ORDER BY expire "
      "LIMIT $3"},

    // GET_LEASE6
    { 0, { OID_NONE },
      "get_lease6",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6"},

    // GET_LEASE6_ADDR
    { 2, { OID_VARCHAR, OID_INT2 },
      "get_lease6_addr",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE address = $1 AND lease_type = $2"},

    // GET_LEASE6_DUID_IAID
    { 3, { OID_BYTEA, OID_INT8, OID_INT2 },
      "get_lease6_duid_iaid",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE duid = $1 AND iaid = $2 AND lease_type = $3"},

    // GET_LEASE6_DUID_IAID_SUBID
    { 4, { OID_INT2, OID_BYTEA, OID_INT8, OID_INT8 },
      "get_lease6_duid_iaid_subid",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE lease_type = $1 "
        "AND duid = $2 AND iaid = $3 AND subnet_id = $4"},

    // GET_LEASE6_PAGE
    { 2, { OID_VARCHAR, OID_INT8 },
      "get_lease6_page",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE address > $1 "
      "ORDER BY address "
      "LIMIT $2"},

    // GET_LEASE6_SUBID
    { 1, { OID_INT8 },
      "get_lease6_subid",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE subnet_id = $1"},

    // GET_LEASE6_DUID
    { 1, { OID_BYTEA },
      "get_lease6_duid",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE duid = $1"},

    // GET_LEASE6_EXPIRE
    { 3, { OID_INT8, OID_TIMESTAMP, OID_INT8 },
      "get_lease6_expire",
      "SELECT address, duid, valid_lifetime, "
        "extract(epoch from expire)::bigint, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, "
        "fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context "
      "FROM lease6 "
      "WHERE state != $1 AND expire < $2 AND valid_lifetime != 4294967295 "
      "ORDER BY expire "
      "LIMIT $3"},

    // INSERT_LEASE4
    { 11, { OID_INT8, OID_BYTEA, OID_BYTEA, OID_INT8, OID_TIMESTAMP, OID_INT8,
            OID_BOOL, OID_BOOL, OID_VARCHAR, OID_INT8, OID_TEXT },
      "insert_lease4",
      "INSERT INTO lease4(address, hwaddr, client_id, "
        "valid_lifetime, expire, subnet_id, fqdn_fwd, fqdn_rev, hostname, "
        "state, user_context) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)"},

    // INSERT_LEASE6
    { 17, { OID_VARCHAR, OID_BYTEA, OID_INT8, OID_TIMESTAMP, OID_INT8,
            OID_INT8, OID_INT2, OID_INT8, OID_INT2, OID_BOOL, OID_BOOL,
            OID_VARCHAR, OID_BYTEA, OID_INT2, OID_INT2, OID_INT8, OID_TEXT },
      "insert_lease6",
      "INSERT INTO lease6(address, duid, valid_lifetime, "
        "expire, subnet_id, pref_lifetime, "
        "lease_type, iaid, prefix_len, fqdn_fwd, fqdn_rev, hostname, "
        "hwaddr, hwtype, hwaddr_source, "
        "state, user_context) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17)"},

    // UPDATE_LEASE4
    { 12, { OID_INT8, OID_BYTEA, OID_BYTEA, OID_INT8, OID_TIMESTAMP, OID_INT8,
            OID_BOOL, OID_BOOL, OID_VARCHAR, OID_INT8, OID_TEXT, OID_INT8 },
      "update_lease4",
      "UPDATE lease4 SET address = $1, hwaddr = $2, "
        "client_id = $3, valid_lifetime = $4, expire = $5, "
        "subnet_id = $6, fqdn_fwd = $7, fqdn_rev = $8, hostname = $9, "
        "state = $10, user_context = $11 "
      "WHERE address = $12"},

    // UPDATE_LEASE6
    { 18, { OID_VARCHAR, OID_BYTEA, OID_INT8, OID_TIMESTAMP, OID_INT8, OID_INT8,
            OID_INT2, OID_INT8, OID_INT2, OID_BOOL, OID_BOOL, OID_VARCHAR,
            OID_BYTEA, OID_INT2, OID_INT2,
            OID_INT8, OID_TEXT, OID_VARCHAR },
      "update_lease6",
      "UPDATE lease6 SET address = $1, duid = $2, "
        "valid_lifetime = $3, expire = $4, subnet_id = $5, "
        "pref_lifetime = $6, lease_type = $7, iaid = $8, "
        "prefix_len = $9, fqdn_fwd = $10, fqdn_rev = $11, hostname = $12, "
        "hwaddr = $13, hwtype = $14, hwaddr_source = $15, "
        "state = $16, user_context = $17 "
      "WHERE address = $18"},
    // ALL_LEASE4_STATS
    { 0, { OID_NONE },
      "all_lease4_stats",
      "SELECT subnet_id, state, leases as state_count"
      "  FROM lease4_stat ORDER BY subnet_id, state"},

    // SUBNET_LEASE4_STATS
    { 1, { OID_INT8 },
      "subnet_lease4_stats",
      "SELECT subnet_id, state, leases as state_count"
      "  FROM lease4_stat "
      "  WHERE subnet_id = $1 "
      "  ORDER BY state"},

    // SUBNET_RANGE_LEASE4_STATS
    { 2, { OID_INT8, OID_INT8 },
      "subnet_range_lease4_stats",
      "SELECT subnet_id, state, leases as state_count"
      "  FROM lease4_stat "
      "  WHERE subnet_id >= $1 and subnet_id <= $2 "
      "  ORDER BY subnet_id, state"},

    // ALL_LEASE6_STATS,
    { 0, { OID_NONE },
     "all_lease6_stats",
     "SELECT subnet_id, lease_type, state, leases as state_count"
     "  FROM lease6_stat ORDER BY subnet_id, lease_type, state" },

    // SUBNET_LEASE6_STATS
    { 1, { OID_INT8 },
      "subnet_lease6_stats",
      "SELECT subnet_id, lease_type, state, leases as state_count"
      "  FROM lease6_stat "
      "  WHERE subnet_id = $1 "
      "  ORDER BY lease_type, state" },

    // SUBNET_RANGE_LEASE6_STATS
    { 2, { OID_INT8, OID_INT8 },
      "subnet_range_lease6_stats",
      "SELECT subnet_id, lease_type, state, leases as state_count"
      "  FROM lease6_stat "
      "  WHERE subnet_id >= $1 and subnet_id <= $2 "
      "  ORDER BY subnet_id, lease_type, state" },
    // End of list sentinel
    { 0,  { 0 }, NULL, NULL}
};

}  // namespace

namespace isc {
namespace dhcp {

/// @brief Base class for marshalling leases to and from PostgreSQL.
///
/// Provides the common functionality to set up binding information between
/// lease objects in the program and their database representation in the
/// database.
class PgSqlLeaseExchange : public PgSqlExchange {
public:
    PgSqlLeaseExchange()
        : addr_str_(""), valid_lifetime_(0), valid_lifetime_str_(""),
          expire_(0), expire_str_(""), subnet_id_(0), subnet_id_str_(""),
          cltt_(0), fqdn_fwd_(false), fqdn_rev_(false), hostname_(""),
          state_str_(""), user_context_("") {
    }

    virtual ~PgSqlLeaseExchange(){}

protected:
    /// @brief Common Instance members used for binding and conversion
    //@{
    std::string            addr_str_;
    uint32_t               valid_lifetime_;
    std::string            valid_lifetime_str_;
    time_t                 expire_;
    std::string            expire_str_;
    uint32_t               subnet_id_;
    std::string            subnet_id_str_;
    time_t                 cltt_;
    bool                   fqdn_fwd_;
    bool                   fqdn_rev_;
    std::string            hostname_;
    std::string            state_str_;
    std::string            user_context_;
    //@}
};

/// @brief Supports exchanging IPv4 leases with PostgreSQL.
class PgSqlLease4Exchange : public PgSqlLeaseExchange {
private:

    /// @brief Column numbers for each column in the Lease4 table.
    /// These are used for both retrieving data and for looking up
    /// column labels for logging.  Note that their numeric order
    /// MUST match that of the column order in the Lease4 table.
    static const size_t ADDRESS_COL = 0;
    static const size_t HWADDR_COL = 1;
    static const size_t CLIENT_ID_COL = 2;
    static const size_t VALID_LIFETIME_COL = 3;
    static const size_t EXPIRE_COL = 4;
    static const size_t SUBNET_ID_COL = 5;
    static const size_t FQDN_FWD_COL = 6;
    static const size_t FQDN_REV_COL = 7;
    static const size_t HOSTNAME_COL = 8;
    static const size_t STATE_COL = 9;
    static const size_t USER_CONTEXT_COL = 10;
    /// @brief Number of columns in the table holding DHCPv4 leases.
    static const size_t LEASE_COLUMNS = 11;

public:

    /// @brief Default constructor
    PgSqlLease4Exchange()
        : lease_(), addr4_(0), hwaddr_length_(0), hwaddr_(hwaddr_length_),
        client_id_length_(0) {

        BOOST_STATIC_ASSERT(9 < LEASE_COLUMNS);

        memset(hwaddr_buffer_, 0, sizeof(hwaddr_buffer_));
        memset(client_id_buffer_, 0, sizeof(client_id_buffer_));

        // Set the column names (for error messages)
        columns_.push_back("address");
        columns_.push_back("hwaddr");
        columns_.push_back("client_id");
        columns_.push_back("valid_lifetime");
        columns_.push_back("expire");
        columns_.push_back("subnet_id");
        columns_.push_back("fqdn_fwd");
        columns_.push_back("fqdn_rev");
        columns_.push_back("hostname");
        columns_.push_back("state");
        columns_.push_back("user_context");
    }

    /// @brief Creates the bind array for sending Lease4 data to the database.
    ///
    /// Converts each Lease4 member into the appropriate form and adds it
    /// to the bind array.  Note that the array additions must occur in the
    /// order the columns are specified in the SQL statement.  By convention
    /// all columns in the table are explicitly listed in the SQL statement(s)
    /// in the same order as they occur in the table.
    ///
    /// @param lease Lease4 object that is to be written to the database
    /// @param[out] bind_array array to populate with the lease data values
    ///
    /// @throw DbOperationError if bind_array cannot be populated.
    void createBindForSend(const Lease4Ptr& lease, PsqlBindArray& bind_array) {
        if (!lease) {
            isc_throw(BadValue, "createBindForSend:: Lease4 object is NULL");
        }

        // Store lease object to ensure it remains valid.
        lease_ = lease;

        try {
            addr_str_ = boost::lexical_cast<std::string>
                        (lease->addr_.toUint32());
            bind_array.add(addr_str_);

            if (lease->hwaddr_ && !lease->hwaddr_->hwaddr_.empty()) {
                // PostgreSql does not provide MAX on variable length types
                // so we have to enforce it ourselves.
                if (lease->hwaddr_->hwaddr_.size() > HWAddr::MAX_HWADDR_LEN) {
                        isc_throw(DbOperationError, "Hardware address length : "
                                  << lease_->hwaddr_->hwaddr_.size()
                                  << " exceeds maximum allowed of: "
                                  << HWAddr::MAX_HWADDR_LEN);
                }
                bind_array.add(lease->hwaddr_->hwaddr_);
            } else {
                bind_array.add("");
            }

            if (lease->client_id_) {
                bind_array.add(lease->client_id_->getClientId());
            } else {
                bind_array.add("");
            }

            valid_lifetime_str_ = boost::lexical_cast<std::string>(lease->valid_lft_);
            bind_array.add(valid_lifetime_str_);

            // Avoid overflow
            uint32_t valid_lft = lease_->valid_lft_;
            if (valid_lft == Lease::INFINITY_LFT) {
                valid_lft = Lease::FIVEHUNDREDDAYS;
            }
            expire_str_ = convertToDatabaseTime(lease->cltt_, valid_lft);
            bind_array.add(expire_str_);

            subnet_id_str_ = boost::lexical_cast<std::string>(lease->subnet_id_);
            bind_array.add(subnet_id_str_);

            bind_array.add(lease->fqdn_fwd_);

            bind_array.add(lease->fqdn_rev_);

            bind_array.add(lease->hostname_);

            state_str_ = boost::lexical_cast<std::string>(lease->state_);
            bind_array.add(state_str_);

            ConstElementPtr ctx = lease->getContext();
            if (ctx) {
                user_context_ = ctx->str();
            } else {
                user_context_ = "";
            }
            bind_array.add(user_context_);

        } catch (const std::exception& ex) {
            isc_throw(DbOperationError,
                      "Could not create bind array from Lease4: "
                      << lease_->addr_.toText() << ", reason: " << ex.what());
        }
    }

    /// @brief Creates a Lease4 object from a given row in a result set.
    ///
    /// @param r result set containing one or rows from the Lease4 table
    /// @param row row number within the result set from to create the Lease4
    /// object.
    ///
    /// @return Lease4Ptr to the newly created Lease4 object
    /// @throw DbOperationError if the lease cannot be created.
    Lease4Ptr convertFromDatabase(const PgSqlResult& r, int row) {
        try {
            getColumnValue(r, row, ADDRESS_COL, addr4_);

            convertFromBytea(r, row, HWADDR_COL, hwaddr_buffer_,
                             sizeof(hwaddr_buffer_), hwaddr_length_);

            convertFromBytea(r, row, CLIENT_ID_COL, client_id_buffer_,
                             sizeof(client_id_buffer_), client_id_length_);

            getColumnValue(r, row, VALID_LIFETIME_COL, valid_lifetime_);

            expire_ = convertFromDatabaseTime(getRawColumnValue(r, row,
                                                                EXPIRE_COL));

            getColumnValue(r, row , SUBNET_ID_COL, subnet_id_);

            // Recover from overflow
            uint32_t valid_lft = valid_lifetime_;
            if (valid_lft == Lease::INFINITY_LFT) {
                valid_lft = Lease::FIVEHUNDREDDAYS;
            }
            cltt_ = expire_ - valid_lft;

            getColumnValue(r, row, FQDN_FWD_COL, fqdn_fwd_);

            getColumnValue(r, row, FQDN_REV_COL, fqdn_rev_);

            hostname_ = getRawColumnValue(r, row, HOSTNAME_COL);

            uint32_t state;
            getColumnValue(r, row , STATE_COL, state);

            HWAddrPtr hwaddr(new HWAddr(hwaddr_buffer_, hwaddr_length_,
                                        HTYPE_ETHER));

            user_context_ = getRawColumnValue(r, row, USER_CONTEXT_COL);
            ConstElementPtr ctx;
            if (!user_context_.empty()) {
                ctx = Element::fromJSON(user_context_);
                if (!ctx || (ctx->getType() != Element::map)) {
                    isc_throw(BadValue, "user context '" << user_context_
                              << "' is not a JSON map");
                }
            }

            Lease4Ptr result(new Lease4(addr4_, hwaddr,
                                         client_id_buffer_, client_id_length_,
                                         valid_lifetime_, cltt_,
                                         subnet_id_, fqdn_fwd_, fqdn_rev_,
                                         hostname_));

            result->state_ = state;

            if (ctx) {
                result->setContext(ctx);
            }

            return (result);
        } catch (const std::exception& ex) {
            isc_throw(DbOperationError,
                      "Could not convert data to Lease4, reason: "
                       << ex.what());
        }
    }

private:
    /// @brief Lease4 object currently being sent to the database.
    /// Storing this value ensures that it remains in scope while any bindings
    /// that refer to its contents are in use.
    Lease4Ptr              lease_;

    /// @brief Lease4 specific members for binding and conversion.
    uint32_t               addr4_;
    size_t                 hwaddr_length_;
    std::vector<uint8_t>   hwaddr_;
    uint8_t                hwaddr_buffer_[HWAddr::MAX_HWADDR_LEN];
    size_t                 client_id_length_;
    uint8_t                client_id_buffer_[ClientId::MAX_CLIENT_ID_LEN];
};

/// @brief Supports exchanging IPv6 leases with PostgreSQL.
class PgSqlLease6Exchange : public PgSqlLeaseExchange {
private:

    /// @brief Column numbers for each column in the Lease6 table.
    /// These are used for both retrieving data and for looking up
    /// column labels for logging.  Note that their numeric order
    /// MUST match that of the column order in the Lease6 table.
    //@{
    static const int ADDRESS_COL = 0;
    static const int DUID_COL = 1;
    static const int VALID_LIFETIME_COL = 2;
    static const int EXPIRE_COL = 3;
    static const int SUBNET_ID_COL = 4;
    static const int PREF_LIFETIME_COL = 5;
    static const int LEASE_TYPE_COL =  6;
    static const int IAID_COL = 7;
    static const int PREFIX_LEN_COL = 8;
    static const int FQDN_FWD_COL = 9;
    static const int FQDN_REV_COL = 10;
    static const int HOSTNAME_COL = 11;
    static const int HWADDR_COL = 12;
    static const int HWTYPE_COL = 13;
    static const int HWADDR_SOURCE_COL = 14;
    static const int STATE_COL = 15;
    static const size_t USER_CONTEXT_COL = 16;
    //@}
    /// @brief Number of columns in the table holding DHCPv6 leases.
    static const size_t LEASE_COLUMNS = 17;

public:

    /// @brief Union for marshalling IAID into and out of the database
    /// IAID is defined in the RFC as 4 octets, which Kea code handles as
    /// a uint32_t.  Postgresql however, offers only signed integer types
    /// of sizes 2, 4, and 8 bytes (SMALLINT, INT, and BIGINT respectively).
    /// IAID is used in several indexes so rather than use the BIGINT, we
    /// use this union to safely move the value into and out of an INT column.
    union Uiaid {
        /// @brief Constructor
        /// @param val unsigned 32 bit value for the IAID.
        Uiaid(uint32_t val) : uval_(val){};

        /// @brief Constructor
        /// @param val signed 32 bit value for the IAID.
        Uiaid(int32_t val) : ival_(val){};

        /// @brief Return a string representing the signed 32-bit value.
        std::string dbInputString() {
            return (boost::lexical_cast<std::string>(ival_));
        };

        uint32_t uval_;
        int32_t ival_;
    };

    PgSqlLease6Exchange()
        : lease_(), duid_length_(0), duid_(), iaid_u_(0), iaid_str_(""),
          lease_type_(Lease6::TYPE_NA), lease_type_str_(""), prefix_len_(0),
          prefix_len_str_(""), pref_lifetime_(0), preferred_lifetime_str_("") {

        BOOST_STATIC_ASSERT(15 < LEASE_COLUMNS);

        memset(duid_buffer_, 0, sizeof(duid_buffer_));

        // Set the column names (for error messages)
        columns_.push_back("address");
        columns_.push_back("duid");
        columns_.push_back("valid_lifetime");
        columns_.push_back("expire");
        columns_.push_back("subnet_id");
        columns_.push_back("pref_lifetime");
        columns_.push_back("lease_type");
        columns_.push_back("iaid");
        columns_.push_back("prefix_len");
        columns_.push_back("fqdn_fwd");
        columns_.push_back("fqdn_rev");
        columns_.push_back("hostname");
        columns_.push_back("hwaddr");
        columns_.push_back("hwtype");
        columns_.push_back("hwaddr_source");
        columns_.push_back("state");
        columns_.push_back("user_context");
    }

    /// @brief Creates the bind array for sending Lease6 data to the database.
    ///
    /// Converts each Lease6 member into the appropriate form and adds it
    /// to the bind array.  Note that the array additions must occur in the
    /// order the columns are specified in the SQL statement.  By convention
    /// all columns in the table are explicitly listed in the SQL statement(s)
    /// in the same order as they occur in the table.
    ///
    /// @param lease Lease6 object that is to be written to the database
    /// @param[out] bind_array array to populate with the lease data values
    ///
    /// @throw DbOperationError if bind_array cannot be populated.
    void createBindForSend(const Lease6Ptr& lease, PsqlBindArray& bind_array) {
        if (!lease) {
            isc_throw(BadValue, "createBindForSend:: Lease6 object is NULL");
        }

        // Store lease object to ensure it remains valid.
        lease_ = lease;
        try {
            addr_str_ = lease_->addr_.toText();
            bind_array.add(addr_str_);

            if (lease_->duid_) {
                bind_array.add(lease_->duid_->getDuid());
            } else {
                isc_throw (BadValue, "IPv6 Lease cannot have a null DUID");
            }

            valid_lifetime_str_ = boost::lexical_cast<std::string>(lease->valid_lft_);
            bind_array.add(valid_lifetime_str_);

            // Avoid overflow
            uint32_t valid_lft = lease_->valid_lft_;
            if (valid_lft == Lease::INFINITY_LFT) {
                valid_lft = Lease::FIVEHUNDREDDAYS;
            }
            expire_str_ = convertToDatabaseTime(lease->cltt_, valid_lft);
            bind_array.add(expire_str_);

            subnet_id_str_ = boost::lexical_cast<std::string>(lease->subnet_id_);
            bind_array.add(subnet_id_str_);

            preferred_lifetime_str_ = boost::lexical_cast<std::string>(lease_->preferred_lft_);
            bind_array.add(preferred_lifetime_str_);

            lease_type_str_ = boost::lexical_cast<std::string>(lease_->type_);
            bind_array.add(lease_type_str_);

            // The iaid is stored as an INT in lease6 table, so we must
            // lexically cast from an integer version to avoid out of range
            // exception failure upon insert.
            iaid_u_.uval_ = lease_->iaid_;
            iaid_str_ = iaid_u_.dbInputString();
            bind_array.add(iaid_str_);

            prefix_len_str_ = boost::lexical_cast<std::string>
                              (static_cast<unsigned int>(lease_->prefixlen_));
            bind_array.add(prefix_len_str_);

            bind_array.add(lease->fqdn_fwd_);

            bind_array.add(lease->fqdn_rev_);

            bind_array.add(lease->hostname_);

            if (lease->hwaddr_ && !lease->hwaddr_->hwaddr_.empty()) {
                // PostgreSql does not provide MAX on variable length types
                // so we have to enforce it ourselves.
                if (lease->hwaddr_->hwaddr_.size() > HWAddr::MAX_HWADDR_LEN) {
                        isc_throw(DbOperationError, "Hardware address length : "
                                  << lease_->hwaddr_->hwaddr_.size()
                                  << " exceeds maximum allowed of: "
                                  << HWAddr::MAX_HWADDR_LEN);
                }
                bind_array.add(lease->hwaddr_->hwaddr_);
            } else {
                bind_array.add("");
            }

            if (lease->hwaddr_) {
                hwtype_str_ = boost::lexical_cast<std::string>
                              (static_cast<unsigned int>(lease_->hwaddr_->htype_));
                hwaddr_source_str_ = boost::lexical_cast<std::string>
                                     (static_cast<unsigned int>(lease_->hwaddr_->source_));
            } else {
                hwtype_str_ = boost::lexical_cast<std::string>
                              (static_cast<unsigned int>(HTYPE_UNDEFINED));
                hwaddr_source_str_ = boost::lexical_cast<std::string>
                                     (static_cast<unsigned int>(HWAddr::HWADDR_SOURCE_UNKNOWN));
            }

            bind_array.add(hwtype_str_);

            bind_array.add(hwaddr_source_str_);

            state_str_ = boost::lexical_cast<std::string>(lease->state_);
            bind_array.add(state_str_);

            ConstElementPtr ctx = lease->getContext();
            if (ctx) {
                user_context_ = ctx->str();
            } else {
                user_context_ = "";
            }
            bind_array.add(user_context_);

        } catch (const std::exception& ex) {
            isc_throw(DbOperationError,
                      "Could not create bind array from Lease6: "
                      << lease_->addr_.toText() << ", reason: " << ex.what());
        }
    }

    /// @brief Creates a Lease6 object from a given row in a result set.
    ///
    /// @param r result set containing one or rows from the Lease6 table
    /// @param row row number within the result set from to create the Lease6
    /// object.
    ///
    /// @return Lease6Ptr to the newly created Lease4 object
    /// @throw DbOperationError if the lease cannot be created.
    Lease6Ptr convertFromDatabase(const PgSqlResult& r, int row) {
        try {

            /// @todo In theory, an administrator could tweak lease
            /// information in the database. In this case, some of the
            /// values could be set to NULL. This is less likely than
            /// in case of host reservations, but we may consider if
            /// retrieved values should be checked for being NULL to
            /// prevent cryptic errors during conversions from NULL
            /// to actual values.

            isc::asiolink::IOAddress addr(getIPv6Value(r, row, ADDRESS_COL));

            convertFromBytea(r, row, DUID_COL, duid_buffer_, sizeof(duid_buffer_), duid_length_);
            DuidPtr duid_ptr(new DUID(duid_buffer_, duid_length_));

            getColumnValue(r, row, VALID_LIFETIME_COL, valid_lifetime_);

            expire_ = convertFromDatabaseTime(getRawColumnValue(r, row,
                                                                EXPIRE_COL));

            // Recover from overflow
            uint32_t valid_lft = valid_lifetime_;
            if (valid_lft == Lease::INFINITY_LFT) {
                valid_lft = Lease::FIVEHUNDREDDAYS;
            }
            cltt_ = expire_ - valid_lft;

            getColumnValue(r, row , SUBNET_ID_COL, subnet_id_);

            getColumnValue(r, row , PREF_LIFETIME_COL, pref_lifetime_);

            getLeaseTypeColumnValue(r, row, LEASE_TYPE_COL, lease_type_);

            getColumnValue(r, row , IAID_COL, iaid_u_.ival_);

            getColumnValue(r, row , PREFIX_LEN_COL, prefix_len_);

            getColumnValue(r, row, FQDN_FWD_COL, fqdn_fwd_);

            getColumnValue(r, row, FQDN_REV_COL, fqdn_rev_);

            hostname_ = getRawColumnValue(r, row, HOSTNAME_COL);

            convertFromBytea(r, row, HWADDR_COL, hwaddr_buffer_,
                             sizeof(hwaddr_buffer_), hwaddr_length_);

            getColumnValue(r, row , HWTYPE_COL, hwtype_);

            getColumnValue(r, row , HWADDR_SOURCE_COL, hwaddr_source_);

            HWAddrPtr hwaddr;

            if (hwaddr_length_) {
                hwaddr.reset(new HWAddr(hwaddr_buffer_, hwaddr_length_,
                                        hwtype_));

                hwaddr->source_ = hwaddr_source_;
            }

            uint32_t state;
            getColumnValue(r, row , STATE_COL, state);

            user_context_ = getRawColumnValue(r, row, USER_CONTEXT_COL);
            ConstElementPtr ctx;
            if (!user_context_.empty()) {
                ctx = Element::fromJSON(user_context_);
                if (!ctx || (ctx->getType() != Element::map)) {
                    isc_throw(BadValue, "user context '" << user_context_
                              << "' is not a JSON map");
                }
            }

            Lease6Ptr result(new Lease6(lease_type_, addr, duid_ptr,
                                        iaid_u_.uval_, pref_lifetime_,
                                        valid_lifetime_,
                                        subnet_id_, fqdn_fwd_, fqdn_rev_,
                                        hostname_, hwaddr, prefix_len_));
            result->cltt_ = cltt_;

            result->state_ = state;

            if (ctx) {
                result->setContext(ctx);
            }

            return (result);
        } catch (const std::exception& ex) {
            isc_throw(DbOperationError,
                      "Could not convert data to Lease6, reason: "
                       << ex.what());
        }
    }

    /// @brief Fetches an integer text column as a Lease6::Type
    ///
    /// @param r the result set containing the query results
    /// @param row the row number within the result set
    /// @param col the column number within the row
    /// @param[out] value parameter to receive the converted value
    ///
    /// Note we depart from overloading getColumnValue to avoid ambiguity
    /// with base class methods for integers.
    ///
    /// @throw  DbOperationError if the value cannot be fetched or is
    /// invalid.
    void getLeaseTypeColumnValue(const PgSqlResult& r, const int row,
                                 const size_t col, Lease6::Type& value) const {
        uint32_t raw_value = 0;
        getColumnValue(r, row , col, raw_value);
        switch (raw_value) {
            case Lease6::TYPE_NA:
            case Lease6::TYPE_TA:
            case Lease6::TYPE_PD:
                value = static_cast<Lease6::Type>(raw_value);
                break;

            default:
                isc_throw(DbOperationError, "Invalid lease type: " << raw_value
                      << " for: " << getColumnLabel(r, col) << " row:" << row);
        }
    }

private:
    /// @brief Lease6 object currently being sent to the database.
    /// Storing this value ensures that it remains in scope while any bindings
    /// that refer to its contents are in use.
    Lease6Ptr              lease_;

    /// @brief Lease6 specific members for binding and conversion.
    //@{
    size_t                 duid_length_;
    vector<uint8_t>        duid_;
    uint8_t                duid_buffer_[DUID::MAX_DUID_LEN];
    union Uiaid            iaid_u_;
    std::string            iaid_str_;
    Lease6::Type           lease_type_;
    std::string            lease_type_str_;
    uint8_t                prefix_len_;
    std::string            prefix_len_str_;
    uint32_t               pref_lifetime_;
    std::string            preferred_lifetime_str_;
    size_t                 hwaddr_length_;
    vector<uint8_t>        hwaddr_;
    uint8_t                hwaddr_buffer_[HWAddr::MAX_HWADDR_LEN];
    uint32_t               hwtype_;
    std::string            hwtype_str_;
    uint32_t               hwaddr_source_;
    std::string            hwaddr_source_str_;
    //@}
};

/// @brief Base PgSql derivation of the statistical lease data query
///
/// This class provides the functionality such as results storage and row
/// fetching common to fulfilling the statistical lease data query.
///
class PgSqlLeaseStatsQuery : public LeaseStatsQuery {
public:
    /// @brief Constructor to query for all subnets' stats
    ///
    ///  The query created will return statistics for all subnets
    ///
    /// @param conn A open connection to the database housing the lease data
    /// @param statement The lease data SQL prepared statement to execute
    /// @param fetch_type Indicates whether or not lease_type should be
    /// fetched from the result set
    PgSqlLeaseStatsQuery(PgSqlConnection& conn, PgSqlTaggedStatement& statement,
                         const bool fetch_type)
        : conn_(conn), statement_(statement), result_set_(), next_row_(0),
         fetch_type_(fetch_type) {
    }

    /// @brief Constructor to query for a single subnet's stats
    ///
    /// The query created will return statistics for a single subnet
    ///
    /// @param conn A open connection to the database housing the lease data
    /// @param statement The lease data SQL prepared statement to execute
    /// @param fetch_type Indicates if query supplies lease type
    /// @param subnet_id id of the subnet for which stats are desired
    PgSqlLeaseStatsQuery(PgSqlConnection& conn, PgSqlTaggedStatement& statement,
                         const bool fetch_type, const SubnetID& subnet_id)
        : LeaseStatsQuery(subnet_id), conn_(conn), statement_(statement), result_set_(),
          next_row_(0), fetch_type_(fetch_type) {
    }

    /// @brief Constructor to query for the stats for a range of subnets
    ///
    /// The query created will return statistics for the inclusive range of
    /// subnets described by first and last sunbet IDs.
    ///
    /// @param conn A open connection to the database housing the lease data
    /// @param statement The lease data SQL prepared statement to execute
    /// @param fetch_type Indicates if query supplies lease type
    /// @param first_subnet_id first subnet in the range of subnets
    /// @param last_subnet_id last subnet in the range of subnets
    PgSqlLeaseStatsQuery(PgSqlConnection& conn, PgSqlTaggedStatement& statement,
                         const bool fetch_type, const SubnetID& first_subnet_id,
                         const SubnetID& last_subnet_id)
        : LeaseStatsQuery(first_subnet_id, last_subnet_id), conn_(conn), statement_(statement),
          result_set_(), next_row_(0), fetch_type_(fetch_type) {
    }

    /// @brief Destructor
    virtual ~PgSqlLeaseStatsQuery() {};

    /// @brief Creates the lease statistical data result set
    ///
    /// The result set is populated by executing a  prepared SQL query
    /// against the database fetches the lease count per lease state per
    /// (per least type - v6 only) per subnet id.
    ///
    /// Depending upon the selection mode, the query will have either no
    /// parameters (for all subnets), a subnet id for a single subnet, or
    /// a first and last subnet id for a subnet range.
    void start() {

        if (getSelectMode() == ALL_SUBNETS) {
            // Run the query with no where clause parameters.
            result_set_.reset(new PgSqlResult(PQexecPrepared(conn_, statement_.name,
                                                             0, 0, 0, 0, 0)));
        } else {
            // Set up the WHERE clause values
            PsqlBindArray parms;

            // Add first_subnet_id used by both single and range.
            std::string subnet_id_str = boost::lexical_cast<std::string>(getFirstSubnetID());
            parms.add(subnet_id_str);

            // Add last_subnet_id for range.
            if (getSelectMode() == SUBNET_RANGE) {
                // Add last_subnet_id used by range.
                string subnet_id_str = boost::lexical_cast<std::string>(getLastSubnetID());
                parms.add(subnet_id_str);
            }

            // Run the query with where clause parameters.
            result_set_.reset(new PgSqlResult(PQexecPrepared(conn_, statement_.name,
                                              parms.size(), &parms.values_[0],
                                              &parms.lengths_[0], &parms.formats_[0], 0)));
        }

        conn_.checkStatementError(*result_set_, statement_);
    }

    /// @brief Fetches the next row in the result set
    ///
    /// Once the internal result set has been populated by invoking the
    /// the start() method, this method is used to iterate over the
    /// result set rows. Once the last row has been fetched, subsequent
    /// calls will return false.
    ///
    /// @param row Storage for the fetched row
    ///
    /// @return True if the fetch succeeded, false if there are no more
    /// rows to fetch.
    bool getNextRow(LeaseStatsRow& row) {
        // If we're past the end, punt.
        if (next_row_ >= result_set_->getRows()) {
            return (false);
        }

        // Fetch the subnet id.
        uint32_t col = 0;
        uint32_t subnet_id;
        PgSqlExchange::getColumnValue(*result_set_, next_row_, col, subnet_id);
        row.subnet_id_ = static_cast<SubnetID>(subnet_id);
        ++col;

        // Fetch the lease type if we were told to do so.
        if (fetch_type_) {
            uint32_t lease_type;
            PgSqlExchange::getColumnValue(*result_set_, next_row_ , col,
                                          lease_type);
            row.lease_type_ = static_cast<Lease::Type>(lease_type);
            ++col;
        } else {
            row.lease_type_ = Lease::TYPE_NA;
        }

        // Fetch the lease state.
        PgSqlExchange::getColumnValue(*result_set_, next_row_ , col,
                                      row.lease_state_);
        ++col;

        // Fetch the state count.
        PgSqlExchange::getColumnValue(*result_set_, next_row_, col,
                                      row.state_count_);

        // Point to the next row.
        ++next_row_;
        return (true);
    }

protected:
    /// @brief Database connection to use to execute the query
    PgSqlConnection& conn_;

    /// @brief The query's prepared statement
    PgSqlTaggedStatement& statement_;

    /// @brief The result set returned by Postgres.
    boost::shared_ptr<PgSqlResult> result_set_;

    /// @brief Index of the next row to fetch
    uint32_t next_row_;

    /// @brief Indicates if query supplies lease type
    bool fetch_type_;
};

PgSqlLeaseMgr::PgSqlLeaseMgr(const DatabaseConnection::ParameterMap& parameters)
    : LeaseMgr(), exchange4_(new PgSqlLease4Exchange()),
    exchange6_(new PgSqlLease6Exchange()), conn_(parameters) {
    conn_.openDatabase();

    // Validate schema version first.
    std::pair<uint32_t, uint32_t> code_version(PG_SCHEMA_VERSION_MAJOR,
                                               PG_SCHEMA_VERSION_MINOR);
    std::pair<uint32_t, uint32_t> db_version = getVersion();
    if (code_version != db_version) {
        isc_throw(DbOpenError,
                  "PostgreSQL schema version mismatch: need version: "
                      << code_version.first << "." << code_version.second
                      << " found version:  " << db_version.first << "."
                      << db_version.second);
    }

    // Now prepare the SQL statements.
    int i = 0;
    for( ; tagged_statements[i].text != NULL ; ++i) {
        conn_.prepareStatement(tagged_statements[i]);
    }

    // Just in case somebody foo-barred things
    if (i != NUM_STATEMENTS) {
        isc_throw(DbOpenError, "Number of statements prepared: " << i
                  << " does not match expected count:" << NUM_STATEMENTS);
    }
}

PgSqlLeaseMgr::~PgSqlLeaseMgr() {
}

std::string
PgSqlLeaseMgr::getDBVersion() {
    std::stringstream tmp;
    tmp << "PostgreSQL backend " << PG_SCHEMA_VERSION_MAJOR;
    tmp << "." << PG_SCHEMA_VERSION_MINOR;
    tmp << ", library " << PQlibVersion();
    return (tmp.str());
}

bool
PgSqlLeaseMgr::addLeaseCommon(StatementIndex stindex,
                              PsqlBindArray& bind_array) {
    PgSqlResult r(PQexecPrepared(conn_, tagged_statements[stindex].name,
                                 tagged_statements[stindex].nbparams,
                                 &bind_array.values_[0],
                                 &bind_array.lengths_[0],
                                 &bind_array.formats_[0], 0));

    int s = PQresultStatus(r);

    if (s != PGRES_COMMAND_OK) {
        // Failure: check for the special case of duplicate entry.  If this is
        // the case, we return false to indicate that the row was not added.
        // Otherwise we throw an exception.
        if (conn_.compareError(r, PgSqlConnection::DUPLICATE_KEY)) {
            return (false);
        }

        conn_.checkStatementError(r, tagged_statements[stindex]);
    }

    return (true);
}

bool
PgSqlLeaseMgr::addLease(const Lease4Ptr& lease) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_ADD_ADDR4).arg(lease->addr_.toText());

    PsqlBindArray bind_array;
    exchange4_->createBindForSend(lease, bind_array);
    return (addLeaseCommon(INSERT_LEASE4, bind_array));
}

bool
PgSqlLeaseMgr::addLease(const Lease6Ptr& lease) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_ADD_ADDR6).arg(lease->addr_.toText());
    PsqlBindArray bind_array;
    exchange6_->createBindForSend(lease, bind_array);

    return (addLeaseCommon(INSERT_LEASE6, bind_array));
}

template <typename Exchange, typename LeaseCollection>
void PgSqlLeaseMgr::getLeaseCollection(StatementIndex stindex,
                                       PsqlBindArray& bind_array,
                                       Exchange& exchange,
                                       LeaseCollection& result,
                                       bool single) const {
    const int n = tagged_statements[stindex].nbparams;
    PgSqlResult r(PQexecPrepared(conn_, tagged_statements[stindex].name, n,
                                 n > 0 ? &bind_array.values_[0] : NULL,
                                 n > 0 ? &bind_array.lengths_[0] : NULL,
                                 n > 0 ? &bind_array.formats_[0] : NULL, 0));

    conn_.checkStatementError(r, tagged_statements[stindex]);

    int rows = PQntuples(r);
    if (single && rows > 1) {
        isc_throw(MultipleRecords, "multiple records were found in the "
                      "database where only one was expected for query "
                      << tagged_statements[stindex].name);
    }

    for(int i = 0; i < rows; ++ i) {
        result.push_back(exchange->convertFromDatabase(r, i));
    }
}

void
PgSqlLeaseMgr::getLease(StatementIndex stindex, PsqlBindArray& bind_array,
                             Lease4Ptr& result) const {
    // Create appropriate collection object and get all leases matching
    // the selection criteria.  The "single" parameter is true to indicate
    // that the called method should throw an exception if multiple
    // matching records are found: this particular method is called when only
    // one or zero matches is expected.
    Lease4Collection collection;
    getLeaseCollection(stindex, bind_array, exchange4_, collection, true);

    // Return single record if present, else clear the lease.
    if (collection.empty()) {
        result.reset();
    } else {
        result = *collection.begin();
    }
}

void
PgSqlLeaseMgr::getLease(StatementIndex stindex, PsqlBindArray& bind_array,
                             Lease6Ptr& result) const {
    // Create appropriate collection object and get all leases matching
    // the selection criteria.  The "single" parameter is true to indicate
    // that the called method should throw an exception if multiple
    // matching records are found: this particular method is called when only
    // one or zero matches is expected.
    Lease6Collection collection;
    getLeaseCollection(stindex, bind_array, exchange6_, collection, true);

    // Return single record if present, else clear the lease.
    if (collection.empty()) {
        result.reset();
    } else {
        result = *collection.begin();
    }
}

Lease4Ptr
PgSqlLeaseMgr::getLease4(const isc::asiolink::IOAddress& addr) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_ADDR4).arg(addr.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // LEASE ADDRESS
    std::string addr_str = boost::lexical_cast<std::string>
                           (addr.toUint32());
    bind_array.add(addr_str);

    // Get the data
    Lease4Ptr result;
    getLease(GET_LEASE4_ADDR, bind_array, result);

    return (result);
}

Lease4Collection
PgSqlLeaseMgr::getLease4(const HWAddr& hwaddr) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_HWADDR).arg(hwaddr.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // HWADDR
    if (!hwaddr.hwaddr_.empty()) {
        bind_array.add(hwaddr.hwaddr_);
    } else {
        bind_array.add("");
    }

    // Get the data
    Lease4Collection result;
    getLeaseCollection(GET_LEASE4_HWADDR, bind_array, result);

    return (result);
}

Lease4Ptr
PgSqlLeaseMgr::getLease4(const HWAddr& hwaddr, SubnetID subnet_id) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_SUBID_HWADDR)
              .arg(subnet_id).arg(hwaddr.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // HWADDR
    if (!hwaddr.hwaddr_.empty()) {
        bind_array.add(hwaddr.hwaddr_);
    } else {
        bind_array.add("");
    }

    // SUBNET_ID
    std::string subnet_id_str = boost::lexical_cast<std::string>(subnet_id);
    bind_array.add(subnet_id_str);

    // Get the data
    Lease4Ptr result;
    getLease(GET_LEASE4_HWADDR_SUBID, bind_array, result);

    return (result);
}

Lease4Collection
PgSqlLeaseMgr::getLease4(const ClientId& clientid) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_CLIENTID).arg(clientid.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // CLIENT_ID
    bind_array.add(clientid.getClientId());

    // Get the data
    Lease4Collection result;
    getLeaseCollection(GET_LEASE4_CLIENTID, bind_array, result);

    return (result);
}

Lease4Ptr
PgSqlLeaseMgr::getLease4(const ClientId&, const HWAddr&, SubnetID) const {
    /// This function is currently not implemented because allocation engine
    /// searches for the lease using HW address or client identifier.
    /// It never uses both parameters in the same time. We need to
    /// consider if this function is needed at all.
    isc_throw(NotImplemented, "The PgSqlLeaseMgr::getLease4 function was"
              " called, but it is not implemented");
}

Lease4Ptr
PgSqlLeaseMgr::getLease4(const ClientId& clientid, SubnetID subnet_id) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_SUBID_CLIENTID)
              .arg(subnet_id).arg(clientid.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // CLIENT_ID
    bind_array.add(clientid.getClientId());

    // SUBNET_ID
    std::string subnet_id_str = boost::lexical_cast<std::string>(subnet_id);
    bind_array.add(subnet_id_str);

    // Get the data
    Lease4Ptr result;
    getLease(GET_LEASE4_CLIENTID_SUBID, bind_array, result);

    return (result);
}

Lease4Collection
PgSqlLeaseMgr::getLeases4(SubnetID subnet_id) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_SUBID4)
        .arg(subnet_id);

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // SUBNET_ID
    std::string subnet_id_str = boost::lexical_cast<std::string>(subnet_id);
    bind_array.add(subnet_id_str);

    // ... and get the data
    Lease4Collection result;
    getLeaseCollection(GET_LEASE4_SUBID, bind_array, result);

    return (result);
}

Lease4Collection
PgSqlLeaseMgr::getLeases4() const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET4);

    // Provide empty binding array because our query has no parameters in
    // WHERE clause.
    PsqlBindArray bind_array;
    Lease4Collection result;
    getLeaseCollection(GET_LEASE4, bind_array, result);

    return (result);
}

Lease4Collection
PgSqlLeaseMgr::getLeases4(const asiolink::IOAddress& lower_bound_address,
                          const LeasePageSize& page_size) const {
    // Expecting IPv4 address.
    if (!lower_bound_address.isV4()) {
        isc_throw(InvalidAddressFamily, "expected IPv4 address while "
                  "retrieving leases from the lease database, got "
                  << lower_bound_address);
    }

    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_PAGE4)
        .arg(page_size.page_size_)
        .arg(lower_bound_address.toText());

    // Prepare WHERE clause
    PsqlBindArray bind_array;

    // Bind lower bound address
    std::string lb_address_data = boost::lexical_cast<std::string>
        (lower_bound_address.toUint32());
    bind_array.add(lb_address_data);

    // Bind page size value
    std::string page_size_data = boost::lexical_cast<std::string>(page_size.page_size_);
    bind_array.add(page_size_data);

    // Get the leases
    Lease4Collection result;
    getLeaseCollection(GET_LEASE4_PAGE, bind_array, result);

    return (result);
}

Lease6Ptr
PgSqlLeaseMgr::getLease6(Lease::Type lease_type,
                         const isc::asiolink::IOAddress& addr) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_ADDR6)
              .arg(addr.toText()).arg(lease_type);

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // LEASE ADDRESS
    std::string addr_str = addr.toText();
    bind_array.add(addr_str);

    // LEASE_TYPE
    std::string type_str_ = boost::lexical_cast<std::string>(lease_type);
    bind_array.add(type_str_);

    // ... and get the data
    Lease6Ptr result;
    getLease(GET_LEASE6_ADDR, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6(Lease::Type lease_type, const DUID& duid,
                          uint32_t iaid) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_IAID_DUID)
              .arg(iaid).arg(duid.toText()).arg(lease_type);

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // DUID
    bind_array.add(duid.getDuid());

    // IAID
    std::string iaid_str = PgSqlLease6Exchange::Uiaid(iaid).dbInputString();
    bind_array.add(iaid_str);

    // LEASE_TYPE
    std::string lease_type_str = boost::lexical_cast<std::string>(lease_type);
    bind_array.add(lease_type_str);

    // ... and get the data
    Lease6Collection result;
    getLeaseCollection(GET_LEASE6_DUID_IAID, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6(Lease::Type lease_type, const DUID& duid,
                          uint32_t iaid, SubnetID subnet_id) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_IAID_SUBID_DUID)
              .arg(iaid).arg(subnet_id).arg(duid.toText()).arg(lease_type);

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // LEASE_TYPE
    std::string lease_type_str = boost::lexical_cast<std::string>(lease_type);
    bind_array.add(lease_type_str);

    // DUID
    bind_array.add(duid.getDuid());

    // IAID
    std::string iaid_str = PgSqlLease6Exchange::Uiaid(iaid).dbInputString();
    bind_array.add(iaid_str);

    // SUBNET ID
    std::string subnet_id_str = boost::lexical_cast<std::string>(subnet_id);
    bind_array.add(subnet_id_str);

    // ... and get the data
    Lease6Collection result;
    getLeaseCollection(GET_LEASE6_DUID_IAID_SUBID, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6(SubnetID subnet_id) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_SUBID6)
        .arg(subnet_id);

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // SUBNET_ID
    std::string subnet_id_str = boost::lexical_cast<std::string>(subnet_id);
    bind_array.add(subnet_id_str);

    // ... and get the data
    Lease6Collection result;
    getLeaseCollection(GET_LEASE6_SUBID, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6(const DUID& duid) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_DUID)
              .arg(duid.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    // DUID
    bind_array.add(duid.getDuid());
    Lease6Collection result;

    // query to fetch the data
    getLeaseCollection(GET_LEASE6_DUID, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6() const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET6);

    // Provide empty binding array because our query has no parameters in
    // WHERE clause.
    PsqlBindArray bind_array;
    Lease6Collection result;
    getLeaseCollection(GET_LEASE6, bind_array, result);

    return (result);
}

Lease6Collection
PgSqlLeaseMgr::getLeases6(const asiolink::IOAddress& lower_bound_address,
                          const LeasePageSize& page_size) const {
    // Expecting IPv6 address.
    if (!lower_bound_address.isV6()) {
        isc_throw(InvalidAddressFamily, "expected IPv6 address while "
                  "retrieving leases from the lease database, got "
                  << lower_bound_address);
    }

    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_PAGE6)
        .arg(page_size.page_size_)
        .arg(lower_bound_address.toText());

    // Prepare WHERE clause
    PsqlBindArray bind_array;

    // In IPv6 we compare addresses represented as strings. The IPv6 zero address
    // is ::, so it is greater than any other address. In this special case, we
    // just use 0 for comparison which should be lower than any real IPv6 address.
    std::string lb_address_data = "0";
    if (!lower_bound_address.isV6Zero()) {
        lb_address_data = lower_bound_address.toText();
    }

    // Bind lower bound address
    bind_array.add(lb_address_data);

    // Bind page size value
    std::string page_size_data = boost::lexical_cast<std::string>(page_size.page_size_);
    bind_array.add(page_size_data);

    // Get the leases
    Lease6Collection result;
    getLeaseCollection(GET_LEASE6_PAGE, bind_array, result);

    return (result);
}

void
PgSqlLeaseMgr::getExpiredLeases4(Lease4Collection& expired_leases,
                                 const size_t max_leases) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_EXPIRED4)
        .arg(max_leases);
    getExpiredLeasesCommon(expired_leases, max_leases, GET_LEASE4_EXPIRE);
}

void
PgSqlLeaseMgr::getExpiredLeases6(Lease6Collection& expired_leases,
                                 const size_t max_leases) const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL, DHCPSRV_PGSQL_GET_EXPIRED6)
        .arg(max_leases);
    getExpiredLeasesCommon(expired_leases, max_leases, GET_LEASE6_EXPIRE);
}

template<typename LeaseCollection>
void
PgSqlLeaseMgr::getExpiredLeasesCommon(LeaseCollection& expired_leases,
                                       const size_t max_leases,
                                       StatementIndex statement_index) const {
    PsqlBindArray bind_array;

    // Exclude reclaimed leases.
    std::string state_str = boost::lexical_cast<std::string>(Lease::STATE_EXPIRED_RECLAIMED);
    bind_array.add(state_str);

    // Expiration timestamp.
    std::string timestamp_str = PgSqlLeaseExchange::convertToDatabaseTime(time(NULL));
    bind_array.add(timestamp_str);

    // If the number of leases is 0, we will return all leases. This is
    // achieved by setting the limit to a very high value.
    uint32_t limit = max_leases > 0 ? static_cast<uint32_t>(max_leases) :
        std::numeric_limits<uint32_t>::max();
    std::string limit_str = boost::lexical_cast<std::string>(limit);
    bind_array.add(limit_str);

    // Retrieve leases from the database.
    getLeaseCollection(statement_index, bind_array, expired_leases);
}

template<typename LeasePtr>
void
PgSqlLeaseMgr::updateLeaseCommon(StatementIndex stindex,
                                 PsqlBindArray& bind_array,
                                 const LeasePtr& lease) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_ADD_ADDR4).arg(tagged_statements[stindex].name);

    PgSqlResult r(PQexecPrepared(conn_, tagged_statements[stindex].name,
                                 tagged_statements[stindex].nbparams,
                                 &bind_array.values_[0],
                                 &bind_array.lengths_[0],
                                 &bind_array.formats_[0], 0));

    conn_.checkStatementError(r, tagged_statements[stindex]);

    int affected_rows = boost::lexical_cast<int>(PQcmdTuples(r));

    // Check success case first as it is the most likely outcome.
    if (affected_rows == 1) {
      return;
    }

    // If no rows affected, lease doesn't exist.
    if (affected_rows == 0) {
        isc_throw(NoSuchLease, "unable to update lease for address " <<
                  lease->addr_.toText() << " as it does not exist");
    }

    // Should not happen - primary key constraint should only have selected
    // one row.
    isc_throw(DbOperationError, "apparently updated more than one lease "
                  "that had the address " << lease->addr_.toText());
}

void
PgSqlLeaseMgr::updateLease4(const Lease4Ptr& lease) {
    const StatementIndex stindex = UPDATE_LEASE4;

    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_UPDATE_ADDR4).arg(lease->addr_.toText());

    // Create the BIND array for the data being updated
    PsqlBindArray bind_array;
    exchange4_->createBindForSend(lease, bind_array);

    // Set up the WHERE clause and append it to the SQL_BIND array
    std::string addr4_ = boost::lexical_cast<std::string>
                         (lease->addr_.toUint32());
    bind_array.add(addr4_);

    // Drop to common update code
    updateLeaseCommon(stindex, bind_array, lease);
}

void
PgSqlLeaseMgr::updateLease6(const Lease6Ptr& lease) {
    const StatementIndex stindex = UPDATE_LEASE6;

    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_UPDATE_ADDR6).arg(lease->addr_.toText());

    // Create the BIND array for the data being updated
    PsqlBindArray bind_array;
    exchange6_->createBindForSend(lease, bind_array);

    // Set up the WHERE clause and append it to the BIND array
    std::string addr_str = lease->addr_.toText();
    bind_array.add(addr_str);

    // Drop to common update code
    updateLeaseCommon(stindex, bind_array, lease);
}

uint64_t
PgSqlLeaseMgr::deleteLeaseCommon(StatementIndex stindex,
                                 PsqlBindArray& bind_array) {
    PgSqlResult r(PQexecPrepared(conn_, tagged_statements[stindex].name,
                                 tagged_statements[stindex].nbparams,
                                 &bind_array.values_[0],
                                 &bind_array.lengths_[0],
                                 &bind_array.formats_[0], 0));

    conn_.checkStatementError(r, tagged_statements[stindex]);
    int affected_rows = boost::lexical_cast<int>(PQcmdTuples(r));

    return (affected_rows);
}

bool
PgSqlLeaseMgr::deleteLease(const isc::asiolink::IOAddress& addr) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_DELETE_ADDR).arg(addr.toText());

    // Set up the WHERE clause value
    PsqlBindArray bind_array;

    if (addr.isV4()) {
        std::string addr4_str = boost::lexical_cast<std::string>
                                 (addr.toUint32());
        bind_array.add(addr4_str);
        return (deleteLeaseCommon(DELETE_LEASE4, bind_array) > 0);
    }

    std::string addr6_str = addr.toText();
    bind_array.add(addr6_str);
    return (deleteLeaseCommon(DELETE_LEASE6, bind_array) > 0);
}

uint64_t
PgSqlLeaseMgr::deleteExpiredReclaimedLeases4(const uint32_t secs) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_DELETE_EXPIRED_RECLAIMED4)
        .arg(secs);
    return (deleteExpiredReclaimedLeasesCommon(secs, DELETE_LEASE4_STATE_EXPIRED));
}

uint64_t
PgSqlLeaseMgr::deleteExpiredReclaimedLeases6(const uint32_t secs) {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_DELETE_EXPIRED_RECLAIMED6)
        .arg(secs);
    return (deleteExpiredReclaimedLeasesCommon(secs, DELETE_LEASE6_STATE_EXPIRED));
}

uint64_t
PgSqlLeaseMgr::deleteExpiredReclaimedLeasesCommon(const uint32_t secs,
                                                  StatementIndex statement_index) {
    PsqlBindArray bind_array;

    // State is reclaimed.
    std::string state_str = boost::lexical_cast<std::string>(Lease::STATE_EXPIRED_RECLAIMED);
    bind_array.add(state_str);

    // Expiration timestamp.
    std::string expiration_str =
        PgSqlLeaseExchange::convertToDatabaseTime(time(NULL) - static_cast<time_t>(secs));
    bind_array.add(expiration_str);

    // Delete leases.
    return (deleteLeaseCommon(statement_index, bind_array));
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startLeaseStatsQuery4() {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[ALL_LEASE4_STATS], false));
    query->start();
    return(query);
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startSubnetLeaseStatsQuery4(const SubnetID& subnet_id) {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[SUBNET_LEASE4_STATS],
                                 false, subnet_id));
    query->start();
    return(query);
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startSubnetRangeLeaseStatsQuery4(const SubnetID& first_subnet_id,
                                                   const SubnetID& last_subnet_id) {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[SUBNET_RANGE_LEASE4_STATS],
                                 false, first_subnet_id, last_subnet_id));
    query->start();
    return(query);
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startLeaseStatsQuery6() {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[ALL_LEASE6_STATS], true));
    query->start();
    return(query);
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startSubnetLeaseStatsQuery6(const SubnetID& subnet_id) {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[SUBNET_LEASE6_STATS],
                                 true, subnet_id));
    query->start();
    return(query);
}

LeaseStatsQueryPtr
PgSqlLeaseMgr::startSubnetRangeLeaseStatsQuery6(const SubnetID& first_subnet_id,
                                                const SubnetID& last_subnet_id) {
    LeaseStatsQueryPtr query(
        new PgSqlLeaseStatsQuery(conn_, tagged_statements[SUBNET_RANGE_LEASE6_STATS],
                                 true, first_subnet_id, last_subnet_id));
    query->start();
    return(query);
}

size_t
PgSqlLeaseMgr::wipeLeases4(const SubnetID& /*subnet_id*/) {
    isc_throw(NotImplemented, "wipeLeases4 is not implemented for PgSQL backend");
}

size_t
PgSqlLeaseMgr::wipeLeases6(const SubnetID& /*subnet_id*/) {
    isc_throw(NotImplemented, "wipeLeases6 is not implemented for PgSQL backend");
}

string
PgSqlLeaseMgr::getName() const {
    string name = "";
    try {
        name = conn_.getParameter("name");
    } catch (...) {
        // Return an empty name
    }
    return (name);
}

string
PgSqlLeaseMgr::getDescription() const {
    return (string("PostgreSQL Database"));
}

pair<uint32_t, uint32_t>
PgSqlLeaseMgr::getVersion() const {
    LOG_DEBUG(dhcpsrv_logger, DHCPSRV_DBG_TRACE_DETAIL,
              DHCPSRV_PGSQL_GET_VERSION);

    const char* version_sql =  "SELECT version, minor FROM schema_version;";
    PgSqlResult r(PQexec(conn_, version_sql));
    if(PQresultStatus(r) != PGRES_TUPLES_OK) {
        isc_throw(DbOperationError, "unable to execute PostgreSQL statement <"
                  << version_sql << ", reason: " << PQerrorMessage(conn_));
    }

    istringstream tmp;
    uint32_t version;
    tmp.str(PQgetvalue(r, 0, 0));
    tmp >> version;
    tmp.str("");
    tmp.clear();

    uint32_t minor;
    tmp.str(PQgetvalue(r, 0, 1));
    tmp >> minor;

    return (make_pair(version, minor));
}

void
PgSqlLeaseMgr::commit() {
    conn_.commit();
}

void
PgSqlLeaseMgr::rollback() {
    conn_.rollback();
}

}; // end of isc::dhcp namespace
}; // end of isc namespace
