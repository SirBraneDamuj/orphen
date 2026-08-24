cmake_minimum_required(VERSION 3.24)

project(orphen_native_port LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ORPHEN_PORT_FETCH_SDL2 "Download SDL2 with CMake FetchContent when SDL2 is not installed" ON)

find_package(OpenGL REQUIRED)

find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    find_package(SDL2 QUIET)
endif()

if(NOT TARGET SDL2::SDL2 AND NOT TARGET SDL2 AND NOT SDL2_FOUND AND ORPHEN_PORT_FETCH_SDL2)
    include(FetchContent)

    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(SDL2)
endif()

add_executable(orphen_port)

target_sources(orphen_port
    PRIVATE
        src/main.cpp
    src/harness/audio_device.cpp
    src/harness/audio_device.h
    src/harness/debug_text.cpp
    src/harness/debug_text.h
    src/harness/entity_probe.cpp
    src/harness/entity_probe.h
    src/harness/map_viewer.cpp
    src/harness/map_viewer.h
    src/harness/disc_resource_loader.cpp
    src/harness/disc_resource_loader.h
    src/harness/scene_resource_provider.cpp
    src/harness/scene_resource_provider.h
    src/harness/scene_resource_tree.cpp
    src/harness/scene_resource_tree.h
        src/harness/flat_bin_archive.cpp
        src/harness/flat_bin_archive.h
        src/ported/entity/map_prop_descriptor_table.cpp
        src/ported/entity/map_prop_descriptor_table.h
        src/ported/debug/original_debug_text.cpp
        src/ported/debug/original_debug_text.h
        src/ported/debug/original_position_display.cpp
        src/ported/debug/original_position_display.h
        src/ported/player/original_chest_cutscene.cpp
        src/ported/player/original_chest_cutscene.h
        src/ported/player/original_item_window.cpp
        src/ported/player/original_item_window.h
        src/ported/resource/item_database.cpp
        src/ported/resource/item_database.h
        src/ported/sound/original_sequence_player.cpp
        src/ported/sound/original_sequence_player.h
        src/ported/sound/original_sound_bank.cpp
        src/ported/sound/original_sound_bank.h
        src/ported/sound/original_sound_engine.cpp
        src/ported/sound/original_sound_engine.h
        src/ported/sound/original_voice_index.cpp
        src/ported/sound/original_voice_index.h
        src/ported/text/original_dialogue_text.cpp
        src/ported/text/original_dialogue_stream.cpp
        src/ported/text/original_dialogue_window.cpp
        src/ported/text/original_dialogue_text.h
        src/ported/text/original_dialogue_stream.h
        src/ported/text/original_dialogue_window.h
        src/ported/render/original_screen_fade.cpp
        src/ported/render/original_screen_fade.h
        src/ported/render/original_letterbox.cpp
        src/ported/render/original_letterbox.h
        src/ported/render/original_fade_track.cpp
        src/ported/render/original_fade_track.h
        src/ported/render/original_light_table.cpp
        src/ported/render/original_light_table.h
        src/platform/gl_extensions.cpp
        src/platform/gl_extensions.h
        src/platform/sdl_gl_window.cpp
        src/platform/sdl_gl_window.h
    src/ported/resource/mcb_table_loader.cpp
    src/ported/resource/mcb_table_loader.h
    src/ported/resource/mcb_scene_bundle_loader.cpp
    src/ported/resource/mcb_scene_bundle_loader.h
    src/ported/resource/mcb_bundle_record_lookup.cpp
    src/ported/resource/mcb_bundle_record_lookup.h
    src/ported/resource/headerless_lz_decoder.cpp
    src/ported/resource/headerless_lz_decoder.h
    src/ported/resource/bmpa_texture_decoder.cpp
    src/ported/resource/bmpa_texture_decoder.h
    src/ported/resource/mcb_runtime.cpp
    src/ported/resource/mcb_runtime.h
    src/ported/resource/elf_data_reader.cpp
    src/ported/resource/elf_data_reader.h
    src/ported/entity/entity_descriptor_table.cpp
    src/ported/entity/entity_descriptor_table.h
    src/ported/entity/entity_pool.cpp
    src/ported/entity/entity_pool.h
    src/ported/entity/original_entity.h
    src/ported/entity/actor_dispatch_table.cpp
    src/ported/entity/actor_dispatch_table.h
    src/ported/entity/actor_frame_update.cpp
    src/ported/entity/entity_collision.cpp
    src/ported/entity/entity_path_follow.cpp
    src/ported/entity/player_bandana.cpp
    src/ported/entity/actor_frame_update.h
    src/ported/entity/original_entity_sound.cpp
    src/ported/entity/original_entity_sound.h
    src/ported/entity/actor_trace.cpp
    src/ported/entity/actor_trace.h
    src/ported/player/original_interaction.cpp
    src/ported/player/original_interaction.h
    src/ported/script/object_registers.cpp
    src/ported/script/object_registers.h
    src/ported/script/scene_command_interpreter.cpp
    src/ported/script/scene_command_interpreter.h
    src/ported/script/scene_script_opcodes.cpp
    src/ported/script/scene_script.cpp
    src/ported/script/scene_script.h
    src/ported/script/script_trace.cpp
    src/ported/script/script_trace.h
    src/ported/psm2/decoded_psm2_loader.cpp
    src/ported/psm2/decoded_psm2_loader.h
    src/ported/psm2/psm2_collision_groups.cpp
    src/ported/psm2/psm2_uv_animation.cpp
    src/ported/psm2/psm2_geometry_builder.cpp
    src/ported/psm2/psm2_geometry_builder.h
    src/ported/psm2/psm2_material_expansion.cpp
    src/ported/psm2/psm2_material_expansion.h
    src/ported/model/psc3_model.cpp
    src/ported/model/psc3_model.h
    src/ported/model/psc3_skeleton.cpp
    src/ported/model/psc3_skeleton.h
    src/ported/model/entity_animation.cpp
    src/ported/model/entity_animation.h
    src/ported/resource/texture_slot_cache.cpp
    src/ported/resource/texture_slot_cache.h
    src/runtime/entity_model_store.cpp
    src/runtime/entity_model_store.h
    src/ported/render/original_view_projection.cpp
    src/ported/render/original_view_projection.h
    src/ported/render/original_map_visibility.cpp
    src/ported/render/original_map_visibility.h
    src/ported/render/original_entity_draw.cpp
    src/ported/render/original_entity_draw.h
    src/ported/render/scene_lighting.h
    src/ported/original_frame_timing.h
    src/ported/psm2/psm2_runtime.h
    src/ported/player/original_player_controller.cpp
    src/ported/player/original_player_controller.h
    src/ported/camera/original_camera_state.cpp
    src/ported/camera/original_camera_state.h
    src/ported/input/original_analog_stick.cpp
    src/ported/input/original_analog_stick.h
    src/ported/camera/original_field_camera.cpp
    src/ported/camera/original_field_camera.h
    src/ported/camera/original_camera_path.cpp
    src/ported/camera/original_camera_path.h
        src/runtime/port_runtime.cpp
        src/runtime/port_runtime.h
        src/runtime/original_lead_player.cpp
        src/runtime/original_lead_player.h
        src/runtime/player_view_state.h
        src/runtime/scene_object_view.h
        src/runtime/input_state.h
        src/runtime/psm2_ground_query.cpp
        src/runtime/psm2_ground_query.h
        src/runtime/ps2_memory.h
)

target_include_directories(orphen_port
    PRIVATE
        src
)

set(ORPHEN_SDL2_TARGETS)

if(TARGET SDL2::SDL2main)
    list(APPEND ORPHEN_SDL2_TARGETS SDL2::SDL2main)
elseif(TARGET SDL2main)
    list(APPEND ORPHEN_SDL2_TARGETS SDL2main)
endif()

if(TARGET SDL2::SDL2)
    list(APPEND ORPHEN_SDL2_TARGETS SDL2::SDL2)
elseif(TARGET SDL2)
    list(APPEND ORPHEN_SDL2_TARGETS SDL2)
elseif(SDL2_FOUND)
    target_include_directories(orphen_port PRIVATE ${SDL2_INCLUDE_DIRS})
    list(APPEND ORPHEN_SDL2_TARGETS ${SDL2_LIBRARIES})
else()
    message(FATAL_ERROR "SDL2 was not found. Install SDL2 or configure with -DORPHEN_PORT_FETCH_SDL2=ON.")
endif()

target_link_libraries(orphen_port
    PRIVATE
        ${ORPHEN_SDL2_TARGETS}
        OpenGL::GL
)

if(MSVC)
    target_compile_options(orphen_port PRIVATE /W4 /permissive-)
else()
    target_compile_options(orphen_port PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET orphen_port POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL2::SDL2>
            $<TARGET_FILE_DIR:orphen_port>
    )
endif()
