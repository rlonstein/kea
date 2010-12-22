// Copyright (C) 2010  Internet Systems Consortium, Inc. ("ISC")
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

#include <set>
#include <string>
#include <utility>
#include <vector>

#include <boost/foreach.hpp>
#include <boost/shared_ptr.hpp>

#include <dns/name.h>
#include <dns/rrclass.h>

#include <cc/data.h>

#include <datasrc/memory_datasrc.h>
#include <datasrc/zonetable.h>

#include <auth/auth_srv.h>
#include <auth/config.h>

using namespace std;
using boost::shared_ptr;
using namespace isc::dns;
using namespace isc::data;
using namespace isc::datasrc;

namespace {
// Forward declaration
AuthConfigParser*
createAuthConfigParser(AuthSrv& server, const std::string& config_id,
                       bool internal);

/// A derived \c AuthConfigParser class for the "datasources" configuration
/// identifier.
class DatasourcesConfig : public AuthConfigParser {
public:
    DatasourcesConfig(AuthSrv& server) : server_(server) {}
    virtual void build(ConstElementPtr config_value);
    virtual void commit();
private:
    AuthSrv& server_;
    vector<shared_ptr<AuthConfigParser> > datasources_;
    set<string> configured_sources_;
};

void
DatasourcesConfig::build(ConstElementPtr config_value) {
    BOOST_FOREACH(ConstElementPtr datasrc_elem, config_value->listValue()) {
        // The caller is supposed to perform syntax-level checks, but we'll
        // do minimum level of validation ourselves so that we won't crash due
        // to a buggy application.
        ConstElementPtr datasrc_type = datasrc_elem->get("type");
        if (!datasrc_type) {
            isc_throw(AuthConfigError, "Missing data source type");
        }

        if (configured_sources_.find(datasrc_type->stringValue()) !=
            configured_sources_.end()) {
            isc_throw(AuthConfigError, "Data source type '" <<
                      datasrc_type->stringValue() << "' already configured");
        }
        
        shared_ptr<AuthConfigParser> datasrc_config =
            shared_ptr<AuthConfigParser>(
                createAuthConfigParser(server_, string("datasources/") +
                                       datasrc_type->stringValue(),
                                       true));
        datasrc_config->build(datasrc_elem);
        datasources_.push_back(datasrc_config);

        configured_sources_.insert(datasrc_type->stringValue());
    }
}

void
DatasourcesConfig::commit() {
    BOOST_FOREACH(shared_ptr<AuthConfigParser> datasrc_config, datasources_) {
        datasrc_config->commit();
    }
}

/// A derived \c AuthConfigParser class for the memory type datasource
/// configuration.  It does not correspond to the configuration syntax;
/// it's instantiated for internal use.
class MemoryDatasourceConfig : public AuthConfigParser {
public:
    MemoryDatasourceConfig(AuthSrv& server) :
        server_(server),
        rrclass_(0)              // XXX: dummy initial value
    {}
    virtual void build(ConstElementPtr config_value);
    virtual void commit();
private:
    AuthSrv& server_;
    RRClass rrclass_;
    AuthSrv::MemoryDataSrcPtr memory_datasrc_;
};

void
MemoryDatasourceConfig::build(ConstElementPtr config_value) {
    // XXX: apparently we cannot retrieve the default RR class from the
    // module spec.  As a temporary workaround we hardcode the default value.
    ConstElementPtr rrclass_elem = config_value->get("class");
    rrclass_ = RRClass(rrclass_elem ? rrclass_elem->stringValue() : "IN");

    // We'd eventually optimize building zones (in case of reloading) by
    // selectively loading fresh zones.  Right now we simply check the
    // RR class is supported by the server implementation.
    server_.getMemoryDataSrc(rrclass_);
    memory_datasrc_ = AuthSrv::MemoryDataSrcPtr(new MemoryDataSrc());

    ConstElementPtr zones_config = config_value->get("zones");
    if (!zones_config) {
        // XXX: Like the RR class, we cannot retrieve the default value here,
        // so we assume an empty zone list in this case.
        return;
    }

    BOOST_FOREACH(ConstElementPtr zone_config, zones_config->listValue()) {
        ConstElementPtr origin = zone_config->get("origin");
        if (!origin) {
            isc_throw(AuthConfigError, "Missing zone origin");
        }
        ConstElementPtr file = zone_config->get("file");
        if (!file) {
            isc_throw(AuthConfigError, "Missing zone file for zone: "
                      << origin->str());
        }
        const result::Result result = memory_datasrc_->addZone(
            ZonePtr(new MemoryZone(rrclass_, Name(origin->stringValue()))));
        if (result == result::EXIST) {
            isc_throw(AuthConfigError, "zone "<< origin->str()
                      << " already exists");
        }

        // TODO
        // then load the zone from 'file', which is currently not implemented.
        //
    }
}

void
MemoryDatasourceConfig::commit() {
    server_.setMemoryDataSrc(rrclass_, memory_datasrc_);
}

// This is a generalized version of create function that can create
// a AuthConfigParser object for "internal" use.
AuthConfigParser*
createAuthConfigParser(AuthSrv& server, const std::string& config_id,
                       bool internal)
{
    // For the initial implementation we use a naive if-else blocks for
    // simplicity.  In future we'll probably generalize it using map-like
    // data structure, and may even provide external register interface so
    // that it can be dynamically customized.
    if (config_id == "datasources") {
        return (new DatasourcesConfig(server));
    } else if (internal && config_id == "datasources/memory") {
        return (new MemoryDatasourceConfig(server));
    } else {
        isc_throw(AuthConfigError, "Unknown configuration variable: " <<
                  config_id);
    }
}
} // end of unnamed namespace

AuthConfigParser*
createAuthConfigParser(AuthSrv& server, const std::string& config_id) {
    return (createAuthConfigParser(server, config_id, false));
}

void
destroyAuthConfigParser(AuthConfigParser* parser) {
    delete parser;
}

void
configureAuthServer(AuthSrv& server, ConstElementPtr config_set) {
    if (!config_set) {
        isc_throw(AuthConfigError,
                  "Null pointer is passed to configuration parser");
    }

    typedef shared_ptr<AuthConfigParser> ParserPtr;
    vector<ParserPtr> parsers;
    typedef pair<string, ConstElementPtr> ConfigPair;
    try {
        BOOST_FOREACH(ConfigPair config_pair, config_set->mapValue()) {
            // We should eventually integrate the sqlite3 DB configuration to
            // this framework, but to minimize diff we begin with skipping that
            // part.
            if (config_pair.first == "database_file") {
                continue;
            }

            ParserPtr parser(createAuthConfigParser(server,
                                                    config_pair.first));
            parser->build(config_pair.second);
            parsers.push_back(parser);
        }
    } catch (const AuthConfigError& ex) {
        throw ex;                  // simply rethrowing it
    } catch (const isc::Exception& ex) {
        isc_throw(AuthConfigError, "Server configuration failed: " <<
                  ex.what());
    }

    BOOST_FOREACH(ParserPtr parser, parsers) {
        parser->commit();
    }
}
