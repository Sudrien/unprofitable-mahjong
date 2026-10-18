# SPDX-License-Identifier: MIT
# The game rules, from ../../core, where test/ can build them on the host
# without IDF. Nothing here but the path.
idf_component_register(
    SRCS "../../core/umcore.c" "../../core/umreplay.c"
    INCLUDE_DIRS "../../core"
)
