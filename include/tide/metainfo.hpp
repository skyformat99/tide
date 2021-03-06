#ifndef TIDE_METAINFO_HEADER
#define TIDE_METAINFO_HEADER

#include "bdecode.hpp"
#include "file_info.hpp"
#include "string_view.hpp"

#include <vector>

namespace tide {

struct metainfo
{
    // This must not be destroyed, for all the below collections point into its
    // source buffer.
    bmap source;

    // This must be present.
    string_view announce;
    string_view piece_hashes;

    struct tracker_entry
    {
        string_view url;
        int tier;
    };

    // http://bittorrent.org/beps/bep_0012.html
    // If this is not empty, only this list will be used, and `announce`
    // disregarded.  The protocol defines this as a list of lists, each
    // representing a tracker, but for better memory layout, a flat list is used
    // and each `tracker_entry` contains the tier to which it belongs.
    std::vector<tracker_entry> announce_list;

    // A position in this vector corresponds to the file(s) specified in
    // metainfo. This vector must contain all the files listed in metainfo, but
    // user may choose which files to download. The files not wanted should be
    // marked with `file.is_wanted` set to false.
    //
    // The reason for providing a separate files list and not using the one in
    // metainfo is that the file paths in metainfo are specified with path
    // elements and may be incorrect, while all `file_info::path`s here are
    // reconstructed to conform to the host's OS, and more importantly,
    // sanitized (invalid path elements are changed or removed, security hazards
    // eliminated etc.), which means that the path here might be different to
    // the one specified in metainfo.
    //
    // The number of files here and in metainfo must match, i.e. unwanted files
    // must not be removed but merely marked as such.
    std::vector<file_info> files;

    int64_t piece_length;
    int64_t total_length;
    int num_pieces;
};

/**
 * This function extracts all commonly used data from the "raw" bmap
 * representing the .torrent/metainfo file, and more importantly, takes care of
 * sanitizing file paths.  The rest of the (optional) information may be
 * manually extracted from metainfo.source.
 *
 * An exception is thrown if any of the required fields are not in source.
 */
metainfo parse_and_sanitize_metainfo(bmap source);

} // tide

#endif // TIDE_METAINFO_HEADER
