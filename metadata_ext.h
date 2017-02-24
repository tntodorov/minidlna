//
// Created by ttodorov on 2/22/17.
//

#ifndef MINIDLNA_METADATA_EXT_H
#define MINIDLNA_METADATA_EXT_H

void
init_ext_meta();

int64_t
search_ext_meta(uint8_t is_tv, const char *path, char *name, int64_t detailID, uint8_t **img, int *img_sz);

#endif //MINIDLNA_METADATA_EXT_H
