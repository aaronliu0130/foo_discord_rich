﻿#include "stdafx.h"

#include "artwork_metadb.h"
#include "discord/uploader.h"
#include "foobar2000/SDK/component.h"

namespace drp
{

// Our group in Properties dialog / Details tab, see track_property_provider_impl
static const char strPropertiesGroup[] = "Discord rich presence";


void generateUrls( metadb_handle_list_cref tracks ) {
    const size_t count = tracks.get_count();
    if (count == 0) return;

    auto client = clientByGUID(guid::artwork_url_index);

    pfc::map_t<metadb_index_hash, metadb_handle_ptr> allHashes;
    for (size_t w = 0; w < count; ++w) {
        metadb_index_hash hash;
        if (client->hashHandle(tracks[w], hash)) {
            if (allHashes.exists(hash)) continue;
            allHashes.set(hash, tracks[w]);
        }
    }

    if (allHashes.get_count() == 0) {
        FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": Could not hash any of the tracks due to unavailable metadata, bailing";
        return;
    }

    auto thread_impl = new service_impl_t<uploader::threaded_process_artwork_uploader>(allHashes);
    const std::string p_title = "Uploading artwork";

    threaded_process::g_run_modeless(
        thread_impl,
        threaded_process::flag_show_progress | threaded_process::flag_show_abort | threaded_process::flag_show_delayed,
        g_foobar2000_api->get_main_window(),
        p_title.c_str(),
        p_title.length()
    );
}

void clearUrls( metadb_handle_list_cref tracks ) {
    const size_t count = tracks.get_count();
    if (count == 0) return;

    auto client = clientByGUID(guid::artwork_url_index);
    pfc::avltree_t<metadb_index_hash> allHashes;

    for (size_t w = 0; w < count; ++w) {
        metadb_index_hash hash;
        if (client->hashHandle(tracks[w], hash)) {
            if (allHashes.exists(hash)) continue;
            allHashes += hash;
        }
    }

    if (allHashes.get_count() == 0) {
        FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": Could not hash any of the tracks due to unavailable metadata, bailing";
        return;
    }

    pfc::list_t<metadb_index_hash> lstChanged; // Linear list of hashes that actually changed

    for (auto iter = allHashes.first(); iter.is_valid(); ++iter)
    {
        const metadb_index_hash hash = *iter;
        if (artwork_url_set( hash, "" ))
        {
            lstChanged += hash;
        }
    }

    FB2K_console_formatter() << DRP_NAME_WITH_VERSION << ": " << lstChanged.get_count() << " entries cleared";
    if (lstChanged.get_count() > 0) {
        // This gracefully tells everyone about what just changed, in one pass regardless of how many items got altered
        cached_index_api()->dispatch_refresh(guid::artwork_url_index, lstChanged);
    }
}

class contextmenu_artwork_url : public contextmenu_item_simple {
public:
	GUID get_parent() {
		return guid::context_menu_group;
	}

	unsigned get_num_items() {
		return 2;
	}

	void get_item_name(unsigned p_index, pfc::string_base & p_out) {
		PFC_ASSERT( p_index < get_num_items() );
		switch(p_index) {
		    case 0:
		        p_out = "Generate artwork url"; break;
		    case 1:
		        p_out = "Clear artwork"; break;
		}
	}

	void context_command(unsigned p_index, metadb_handle_list_cref p_data, const GUID& p_caller) {
		PFC_ASSERT( p_index < get_num_items() );

        switch (p_index)
        {
        case 0:
            generateUrls( p_data );
            break;
        case 1:
            clearUrls( p_data );
            break;
        default:
            uBugCheck();
        }
	}

	GUID get_item_guid(unsigned p_index) {
		switch(p_index) {
		    case 0:	return guid::context_menu_item_generate_url;
		    case 1:	return guid::context_menu_item_clear_url;
		    default: uBugCheck();
		}
	}

	bool get_item_description(unsigned p_index, pfc::string_base & p_out) {
		PFC_ASSERT( p_index < get_num_items() );
		switch( p_index ) {
		case 0:
			p_out = "Generate urls to be used with discord rich presence";
			return true;
		case 1:
		    // Currently clears based on album. Can be changed after a good way of storing image hashes is found
		    p_out = "Clear artwork urls of selected albums";
		    return true;
		default:
			PFC_ASSERT(!"Should not get here");
			return false;
		}
	}
};

static contextmenu_group_popup_factory g_mygroup( guid::context_menu_group, contextmenu_groups::root, "Discord rich presence", 0 );
static contextmenu_item_factory_t< contextmenu_artwork_url > g_contextmenu_rating;


class track_property_provider_impl : public track_property_provider_v2 {
public:
	void workThisIndex(GUID const & whichID, double priorityBase, metadb_handle_list_cref p_tracks, track_property_callback & p_out) {
		auto client = clientByGUID( whichID );
		pfc::avltree_t<metadb_index_hash> hashes;
		const size_t trackCount = p_tracks.get_count();
		for (size_t trackWalk = 0; trackWalk < trackCount; ++trackWalk) {
			metadb_index_hash hash;
			if (client->hashHandle(p_tracks[trackWalk], hash)) {
				hashes += hash;
			}
		}

		pfc::string8 strComment;

		{
			bool bFirst = true;
			bool bVarComments = false;
			for (auto i = hashes.first(); i.is_valid(); ++i) {
				auto rec = record_get( *i );


				if ( bFirst ) {
					strComment = rec.artwork_url;
				} else if ( ! bVarComments ) {
					if ( strComment != rec.artwork_url ) {
						bVarComments = true;
						strComment = "<various>";
					}
				}

				bFirst = false;
			}
		}

		p_out.set_property(strPropertiesGroup, priorityBase, PFC_string_formatter() << "Artwork Url", strComment);
	}
	void enumerate_properties(metadb_handle_list_cref p_tracks, track_property_callback & p_out) {
		workThisIndex( guid::artwork_url_index, 0, p_tracks, p_out );
	}
	void enumerate_properties_v2(metadb_handle_list_cref p_tracks, track_property_callback_v2 & p_out) {
		if ( p_out.is_group_wanted( strPropertiesGroup ) ) {
			enumerate_properties( p_tracks, p_out );
		}
	}

	bool is_our_tech_info(const char * p_name) {
		// If we do stuff with tech infos read from the file itself (see file_info::info_* methods), signal whether this field belongs to us
		// We don't do any of this, hence false
		return false;
	}
};

static service_factory_single_t<track_property_provider_impl> g_track_property_provider_impl;
}
