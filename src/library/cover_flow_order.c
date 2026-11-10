#include "library/cover_flow_order.h"

CoverFlowOrder cover_flow_order_make(const Library *library)
{
    uint32_t groups = library_music_count(library, LIBRARY_ALBUM_ARTISTS);
    CoverFlowOrder order = {.first_link = 0, .count = 0, .linked = groups > 0};
    uint32_t next = 0;
    for (uint32_t index = 0; index < groups && order.linked; index++) {
        LibraryGroup group;
        if (!library_read_group(library, LIBRARY_ALBUM_ARTISTS, index, &group) ||
            (index > 0 && group.first_link != next)) {
            order.linked = false;
            break;
        }
        order.first_link = index == 0 ? group.first_link : order.first_link;
        next = group.first_link + group.link_count;
    }
    order.count =
        order.linked ? next - order.first_link : library_music_count(library, LIBRARY_ALBUMS);
    return order;
}

bool cover_flow_order_album(const Library *library, CoverFlowOrder order, uint32_t position,
                            uint16_t *album)
{
    if (position >= order.count) {
        return false;
    }
    if (!order.linked) {
        *album = (uint16_t)position;
        return true;
    }
    return library_read_index(library, LIBRARY_LINKS, order.first_link + position, album);
}

bool cover_flow_order_find(const Library *library, CoverFlowOrder order, uint16_t album,
                           uint32_t *position)
{
    LibraryAlbum record;
    LibraryGroup group;
    if (!library_read_album(library, album, &record) || album >= order.count) {
        return false;
    }
    if (!order.linked) {
        *position = album;
        return true;
    }
    if (!library_read_group(library, LIBRARY_ALBUM_ARTISTS, record.album_artist, &group)) {
        return false;
    }
    for (uint32_t link = 0; link < group.link_count; link++) {
        uint16_t value = 0;
        if (library_read_index(library, LIBRARY_LINKS, group.first_link + link, &value) &&
            value == album) {
            *position = group.first_link + link - order.first_link;
            return true;
        }
    }
    return false;
}
