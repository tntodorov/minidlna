//
// Created by ttodorov on 2/22/17.
//

#ifndef MINIDLNA_METADATA_EXT_H
#define MINIDLNA_METADATA_EXT_H

void
init_ext_meta();

int64_t
search_ext_meta(const char *path, char *name, int64_t detailID);

#endif //MINIDLNA_METADATA_EXT_H
