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

#ifndef CFG_OPTION_H
#define CFG_OPTION_H

#include <dhcp/option.h>
#include <dhcpsrv/key_from_key.h>
#include <dhcpsrv/option_space_container.h>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <stdint.h>
#include <string>

namespace isc {
namespace dhcp {

/// @brief Option descriptor.
///
/// Option descriptor holds instance of an option and additional information
/// for this option. This information comprises whether this option is sent
/// to DHCP client only on request (persistent = false) or always
/// (persistent = true).
struct OptionDescriptor {
    /// Option instance.
    OptionPtr option;
    /// Persistent flag, if true option is always sent to the client,
    /// if false option is sent to the client on request.
    bool persistent;

    /// @brief Constructor.
    ///
    /// @param opt option
    /// @param persist if true option is always sent.
    OptionDescriptor(const OptionPtr& opt, bool persist)
        : option(opt), persistent(persist) {};

    /// @brief Constructor
    ///
    /// @param persist if true option is always sent.
    OptionDescriptor(bool persist)
        : option(OptionPtr()), persistent(persist) {};
};

/// A pointer to option descriptor.
typedef boost::shared_ptr<OptionDescriptor> OptionDescriptorPtr;

/// @brief Multi index container for DHCP option descriptors.
///
/// This container comprises three indexes to access option
/// descriptors:
/// - sequenced index: used to access elements in the order they
/// have been added to the container,
/// - option type index: used to search option descriptors containing
/// options with specific option code (aka option type).
/// - persistency flag index: used to search option descriptors with
/// 'persistent' flag set to true.
///
/// This container is the equivalent of three separate STL containers:
/// - std::list of all options,
/// - std::multimap of options with option code used as a multimap key,
/// - std::multimap of option descriptors with option persistency flag
/// used as a multimap key.
/// The major advantage of this container over 3 separate STL containers
/// is automatic synchronization of all indexes when elements are added,
/// removed or modified in the container. With separate containers,
/// the synchronization would have to be guaranteed by the Subnet class
/// code. This would increase code complexity and presumably it would
/// be much harder to add new search criteria (indexes).
///
/// @todo we may want to search for options using option spaces when
/// they are implemented.
///
/// @see http://www.boost.org/doc/libs/1_51_0/libs/multi_index/doc/index.html
typedef boost::multi_index_container<
    // Container comprises elements of OptionDescriptor type.
    OptionDescriptor,
    // Here we start enumerating various indexes.
    boost::multi_index::indexed_by<
        // Sequenced index allows accessing elements in the same way
        // as elements in std::list.
        // Sequenced is an index #0.
        boost::multi_index::sequenced<>,
        // Start definition of index #1.
        boost::multi_index::hashed_non_unique<
            // KeyFromKeyExtractor is the index key extractor that allows
            // accessing option type being held by the OptionPtr through
            // OptionDescriptor structure.
            KeyFromKeyExtractor<
                // Use option type as the index key. The type is held
                // in OptionPtr object so we have to call Option::getType
                // to retrieve this key for each element.
                boost::multi_index::const_mem_fun<
                    Option,
                    uint16_t,
                    &Option::getType
                >,
                // Indicate that OptionPtr is a member of
                // OptionDescriptor structure.
                boost::multi_index::member<
                    OptionDescriptor,
                    OptionPtr,
                    &OptionDescriptor::option
                 >
            >
        >,
        // Start definition of index #2.
        // Use 'persistent' struct member as a key.
        boost::multi_index::hashed_non_unique<
            boost::multi_index::member<
                OptionDescriptor,
                bool,
                &OptionDescriptor::persistent
            >
        >
    >
> OptionContainer;

/// Pointer to the OptionContainer object.
typedef boost::shared_ptr<OptionContainer> OptionContainerPtr;
/// Type of the index #1 - option type.
typedef OptionContainer::nth_index<1>::type OptionContainerTypeIndex;
/// Pair of iterators to represent the range of options having the
/// same option type value. The first element in this pair represents
/// the beginning of the range, the second element represents the end.
typedef std::pair<OptionContainerTypeIndex::const_iterator,
                  OptionContainerTypeIndex::const_iterator> OptionContainerTypeRange;
/// Type of the index #2 - option persistency flag.
typedef OptionContainer::nth_index<2>::type OptionContainerPersistIndex;

/// @brief Represents option data configuration for the DHCP server.
///
/// This class holds a collection of options to be sent to a DHCP client.
/// Options are grouped by the option space or vendor identifier (for
/// vendor options).
///
/// The server configuration allows for specifying two distinct collections
/// of options: global options and per-subnet options in which some options
/// may overlap.
///
/// The collection of global options specify options being sent to the client
/// belonging to any subnets, i.e. global options are "inherited" by all
/// subnets.
///
/// The per-subnet options are configured for a particular subnet and are sent
/// to clients which belong to this subnet. The values of the options specified
/// for a particular subnet override the values of the global options.
///
/// This class represents a single collection of options (either global or
/// per-subnet). Each subnet holds its own object of the @c CfgOption type. The
/// @c CfgMgr holds a @c CfgOption object representing global options.
///
/// Note that having a separate copy of the @c CfgOption to represent global
/// options is useful when the client requests stateless configuration from
/// the DHCP server and no subnet is selected for this client. This client
/// will only receive global options.
class CfgOption {
public:

    /// @brief Adds instance of the option to the configuration.
    ///
    /// There are two types of options which may be passed to this method:
    /// - vendor options
    /// - non-vendor options
    ///
    /// The non-vendor options are grouped by the name of the option space
    /// (specified in textual format). The vendor options are grouped by the
    /// vendor identifier, which is a 32-bit unsigned integer value.
    ///
    /// In order to add new vendor option to the list the option space name
    /// (last argument of this method) should be specified as "vendor-X" where
    /// "X" is a 32-bit unsigned integer, e.g. "vendor-1234". Options for which
    /// the @c option_space argument doesn't follow this format are added as
    /// non-vendor options.
    ///
    /// @param option Pointer to the option being added.
    /// @param persistent Boolean value which specifies if the option should
    /// be sent to the client regardless if requested (true), or nor (false)
    /// @param option_space Option space name.
    ///
    /// @throw isc::BadValue if the option space is invalid.
    void add(const OptionPtr& option, const bool persistent,
             const std::string& option_space);

    /// @brief Returns all options for the specified option space.
    ///
    /// This method will not return vendor options, i.e. having option space
    /// name in the format of "vendor-X" where X is 32-bit unsiged integer.
    ///
    /// @param option_space Name of the option space.
    ///
    /// @return Pointer to the container holding returned options. This
    /// container is empty if no options have been found.
    OptionContainerPtr getAll(const std::string& option_space) const;

    /// @brief Returns vendor options for the specified vendor id.
    ///
    /// @param vendor_id Vendor id for which options are to be returned.
    ///
    /// @return Pointer to the container holding returned options. This
    /// container is empty if no options have been found.
    OptionContainerPtr getAll(const uint32_t vendor_id) const;

    /// @brief Returns option for the specified key and option code.
    ///
    /// The key should be a string, in which case it specifies an option space
    /// name, or an uint32_t value, in which case it specifies a vendor
    /// identifier.
    ///
    /// @param key Option space name or vendor identifier.
    /// @param option_code Code of the option to be returned.
    /// @tparam T one of: @c std::string or @c uint32_t
    ///
    /// @return Descriptor of the option. If option hasn't been found, the
    /// descriptor holds NULL option.
    template<typename T>
    OptionDescriptor get(const T& key, const uint16_t option_code) const {
        OptionContainerPtr options = getAll(key);
        if (!options || options->empty()) {
            return (OptionDescriptor(false));
        }

        const OptionContainerTypeIndex& idx = options->get<1>();
        const OptionContainerTypeRange& range = idx.equal_range(option_code);
        if (std::distance(range.first, range.second) == 0) {
            return (OptionDescriptor(false));
        }

        return (*range.first);
    }

private:

    /// @brief Type of the container holding options grouped by option space.
    typedef OptionSpaceContainer<OptionContainer, OptionDescriptor,
                                 std::string> OptionSpaceCollection;
    /// @brief Container holding options grouped by option space.
    OptionSpaceCollection options_;

    /// @brief Type of the container holding options grouped by vendor id.
    typedef OptionSpaceContainer<OptionContainer, OptionDescriptor,
                                 uint32_t> VendorOptionSpaceCollection;
    /// @brief Container holding options grouped by vendor id.
    VendorOptionSpaceCollection vendor_options_;



};

}
}

#endif // CFG_OPTION_H
