/*
 * Copyright (C) 2006-2012 Christopho, Solarus - http://www.solarus-games.org
 *
 * Solarus is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Solarus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "lua/LuaContext.h"
#include "entities/DestinationPoint.h"
#include "entities/Switch.h"
#include "entities/Sensor.h"
#include "entities/NPC.h"
#include "entities/Chest.h"
#include "entities/ShopItem.h"
#include "entities/Door.h"
#include "entities/Block.h"
#include "entities/CustomEnemy.h"
#include "entities/PickableItem.h"
#include "lowlevel/FileTools.h"
#include "lowlevel/Debug.h"
#include "lowlevel/StringConcat.h"
#include "EquipmentItem.h"
#include "Treasure.h"
#include "Map.h"
#include <sstream>
#include <iomanip>
#include <lua.hpp>

/**
 * @brief Creates a Lua context.
 * @param main_loop The Solarus main loop manager.
 */
LuaContext::LuaContext(MainLoop& main_loop):
  Script(main_loop) {

}

/**
 * @brief Destroys this Lua context.
 */
LuaContext::~LuaContext() {

  this->exit();
}

/**
 * @brief Initializes Lua.
 */
void LuaContext::initialize() {

  Script::initialize();
  register_video_module();
  register_menu_module();
  register_language_module();

  // Make require() able to load Lua files even from the data.solarus archive.
                                  // ...
  lua_getglobal(l, "sol");
                                  // ... sol
  lua_pushcfunction(l, l_loader);
                                  // ... sol loader
  lua_setfield(l, -2, "loader");
                                  // ... sol
  luaL_dostring(l, "table.insert(package.loaders, 2, sol.loader)");
                                  // ... sol
  lua_pushnil(l);
                                  // ... sol nil
  lua_setfield(l, -2, "loader");
                                  // ... sol
  lua_pop(l, 1);
                                  // ...

  // Execute the main file.
  do_file(l, "main");
  main_on_started();
}

/**
 * @brief Cleans Lua.
 */
void LuaContext::exit() {

  if (l != NULL) {
    main_on_finished();
  }
  Script::exit();
}

/**
 * @brief A loader that makes require() able to load quest scripts.
 * @param l the Lua context that is calling this function
 * @return number of values to return to Lua
 */
int LuaContext::l_loader(lua_State* l) {

  const std::string& script_name = luaL_checkstring(l, 1);
  bool exists = load_file_if_exists(l, script_name);

  if (!exists) {
    std::ostringstream oss;
    oss << std::endl << "\tno quest file '" << script_name
      << ".lua' in 'data' or 'data.solarus'";
    lua_pushstring(l, oss.str().c_str());
  }
  return 1;
}

/**
 * @brief Gets a local Lua function from the environment of another one
 * on top of the stack.
 *
 * This is equivalent to find_local_function(-1, function_name).
 *
 * @param function_name Name of the function to find in the environment of the
 * first one.
 * @return true if the function was found.
 */
bool LuaContext::find_local_function(const std::string& function_name) {

  return find_local_function(-1, function_name);
}

/**
 * @brief Gets a local Lua function from the environment of another one.
 *
 * The function found is placed on top the stack if it exists.
 * If the function is not found, the stack is left unchanged.
 *
 * @param index Index of an existing function in the stack.
 * @param function_name Name of the function to find in the environment of the
 * first one.
 * @return true if the function was found.
 */
bool LuaContext::find_local_function(int index, const std::string& function_name) {

                                  // ... f1 ...
  lua_getfenv(l, index);
                                  // ... f1 ... env
  lua_getfield(l, -1, function_name.c_str());
                                  // ... f1 ... env f2/?
  bool exists = lua_isfunction(l, -1);

  // Restore the stack.
  if (exists) {
    lua_remove(l, -2);
                                  // ... f1 ... f2
  }
  else {
    lua_pop(l, 2);
                                  // ... f1 ...
  }

  return exists;
}

/**
 * @brief Gets a method of the object on top of the stack.
 *
 * This is equivalent to find_method(-1, function_name).
 *
 * @param function_name Name of the function to find in the object.
 * @return true if the function was found.
 */
bool LuaContext::find_method(const std::string& function_name) {

  return find_method(-1, function_name);
}

/**
 * @brief Gets a method of an object.
 *
 * If the method exists, the method and the object are both pushed
 * so that you can call the method immediately with the object as first parameter.
 * If the method is not found, the stack is left unchanged.
 *
 * @param index Index of the object in the stack.
 * @param function_name Name of the function to find in the object.
 * @return true if the function was found.
 */
bool LuaContext::find_method(int index, const std::string& function_name) {

  index = get_positive_index(l, index);

                                  // ... object ...
  lua_getfield(l, index, function_name.c_str());
                                  // ... object ... method/?

  bool exists = lua_isfunction(l, -1);

  if (exists) {
                                  // ... object ... method
    lua_pushvalue(l, index);
                                  // ... object ... method object
  }
  else {
    // Restore the stack.
    lua_pop(l, 1);
                                  // ... object ...
  }

  return exists;
}

/**
 * @brief Updates the Lua world.
 *
 * This function is called at each cycle.
 * sol.main.on_update() is called if it exists.
 */
void LuaContext::update() {

  Script::update();

  // Call sol.main.on_update().
  main_on_update();
}

/**
 * @brief Notifies the general Lua script that an input event has just occurred.
 *
 * The appropriate callback in sol.main is notified.
 *
 * @param event The input event to handle.
 */
void LuaContext::notify_input(InputEvent& event) {

  // Call the appropriate callback in sol.main (if it exists).
  main_on_input(event);
}

/**
 * @brief Notifies the Lua world that a map has just been started.
 * @param map The map started.
 * @param destination_point The destination point used if it's a normal one,
 * NULL otherwise.
 */
void LuaContext::notify_map_started(Map& map, DestinationPoint* destination_point) {

  // Compute the file name, depending on the id of the map.
  int id = (int) map.get_id();
  std::ostringstream oss;
  oss << "maps/map" << std::setfill('0') << std::setw(4) << id;
  std::string file_name(oss.str());

  // Load the map's code.
  load_file(l, file_name);

  // Run it with the map userdata as parameter.
  push_map(l, map);
  call_function(1, 0, file_name);

  // Call the map:on_started callback.
  map_on_started(map, destination_point);
}

/**
 * @brief Notifies the Lua world that an equipment item has just been created.
 * @param item The item.
 */
void LuaContext::notify_item_created(EquipmentItem& item) {

  // Compute the file name, depending on the id of the equipment item.
  std::string file_name = (std::string) "items/" + item.get_name();

  // Load the item's code.
  if (load_file_if_exists(l, file_name)) {

    // Run it with the item userdata as parameter.
    push_item(l, item);
    call_function(1, 0, file_name);
  }
}

/**
 * @brief Notifies the Lua world that an enemy has just been added to the map.
 * @param item The item.
 */
void LuaContext::notify_enemy_created(CustomEnemy& enemy) {

  // Compute the file name, depending on enemy's breed.
  std::string file_name = (std::string) "enemies/" + enemy.get_breed();

  // Load the enemy's code.
  if (load_file_if_exists(l, file_name)) {

    // Run it with the enemy userdata as parameter.
    push_enemy(l, enemy);
    call_function(1, 0, file_name);

    enemy_on_suspended(enemy, enemy.is_suspended());
    enemy_on_appear(enemy);
  }
}

/**
 * @brief Calls the on_started() method of the object on top of the stack.
 */
void LuaContext::on_started() {

  if (find_method("on_started")) {
    call_function(1, 0, "on_started");
  }
}

/**
 * @brief Calls the on_finished() method of the object on top of the stack.
 */
void LuaContext::on_finished() {

  if (find_method("on_finished")) {
    call_function(1, 0, "on_finished");
  }
}

/**
 * @brief Calls the on_update() method of the object on top of the stack.
 */
void LuaContext::on_update() {

  if (find_method("on_update")) {
    call_function(1, 0, "on_update");
  }
}

/**
 * @brief Calls the on_display() method of the object on top of the stack.
 * @param dst_surface The destination surface.
 */
void LuaContext::on_display(Surface& dst_surface) {

  if (find_method("on_display")) {
    push_surface(l, dst_surface);
    call_function(2, 0, "on_display");
  }
}

/**
 * @brief Calls the on_pre_display() method of the object on top of the stack.
 * @param dst_surface The destination surface.
 */
void LuaContext::on_pre_display(Surface& dst_surface) {

  if (find_method("on_pre_display")) {
    push_surface(l, dst_surface);
    call_function(2, 0, "on_pre_display");
  }
}

/**
 * @brief Calls the on_post_display() method of the object on top of the stack.
 * @param dst_surface The destination surface.
 */
void LuaContext::on_post_display(Surface& dst_surface) {

  if (find_method("on_post_display")) {
    push_surface(l, dst_surface);
    call_function(2, 0, "on_post_display");
  }
}

/**
 * @brief Calls the on_suspended() method of the object on top of the stack.
 * @param suspended true to suspend the object, false to unsuspend it.
 */
void LuaContext::on_suspended(bool suspended) {

  if (find_method("on_suspended")) {
    lua_pushboolean(l, suspended);
    call_function(2, 0, "on_suspended");
  }
}

/**
 * @brief Calls an input callback method of the object on top of the stack.
 * @param event The input event to forward.
 */
void LuaContext::on_input(InputEvent& event) {

  // Call the Lua function(s) corresponding to this input event.
  if (event.is_keyboard_event()) {
    // Keyboard.
    if (event.is_keyboard_key_pressed()) {
      on_key_pressed(event);
    }
    else if (event.is_keyboard_key_released()) {
      on_key_released(event);
    }
  }
  else if (event.is_joypad_event()) {
    // Joypad.
    if (event.is_joypad_button_pressed()) {
      on_joypad_button_pressed(event);
    }
    else if (event.is_joypad_button_released()) {
      on_joypad_button_released(event);
    }
    else if (event.is_joypad_axis_moved()) {
      on_joypad_axis_moved(event);
    }
    else if (event.is_joypad_hat_moved()) {
      on_joypad_hat_moved(event);
    }
  }

  if (event.is_direction_pressed()) {
    // Keyboard or joypad direction.
    on_direction_pressed(event);
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a keyboard key was just pressed
 * (including if it is a directional key).
 * @param event The corresponding input event.
 */
void LuaContext::on_key_pressed(InputEvent& event) {

  if (find_method("on_key_pressed")) {

    const std::string& key_name = input_get_key_name(event.get_keyboard_key());
    if (!key_name.empty()) { // This key exists in the Lua API.

      lua_pushstring(l, key_name.c_str());
      lua_newtable(l);

      if (event.is_with_shift()) {
        lua_pushboolean(l, 1);
        lua_setfield(l, -2, "shift");
      }

      if (event.is_with_control()) {
        lua_pushboolean(l, 1);
        lua_setfield(l, -2, "control");
      }

      if (event.is_with_alt()) {
        lua_pushboolean(l, 1);
        lua_setfield(l, -2, "alt");
      }
      call_function(3, 0, "on_key_pressed");
    }
    else {
      // The method exists but the key is unknown.
      lua_pop(l, 1);
    }
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a keyboard key was just released
 * (including if it is a directional key).
 * @param event The corresponding input event.
 */
void LuaContext::on_key_released(InputEvent& event) {

  if (find_method("on_key_released")) {

    const std::string& key_name = input_get_key_name(event.get_keyboard_key());
    if (!key_name.empty()) { // This key exists in the Lua API.
      lua_pushstring(l, key_name.c_str());
      call_function(2, 0, "on_key_released");
    }
    else {
      // The method exists but the key is unknown.
      lua_pop(l, 1);
    }
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a joypad button was just pressed.
 * @param event The corresponding input event.
 */
void LuaContext::on_joypad_button_pressed(InputEvent& event) {

  if (find_method("on_joyad_button_pressed")) {
    int button = event.get_joypad_button();

    lua_pushinteger(l, button);
    call_function(2, 0, "on_joyad_button_pressed");
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a joypad button was just released.
 * @param event The corresponding input event.
 */
void LuaContext::on_joypad_button_released(InputEvent& event) {

  if (find_method("on_joyad_button_released")) {
    int button = event.get_joypad_button();

    lua_pushinteger(l, button);
    call_function(2, 0, "on_joyad_button_released");
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a joypad axis was just moved.
 * @param event The corresponding input event.
 */
void LuaContext::on_joypad_axis_moved(InputEvent& event) {

  if (find_method("on_joyad_axis_moved")) {
    int axis = event.get_joypad_axis();
    int state = event.get_joypad_axis_state();

    lua_pushinteger(l, axis);
    lua_pushinteger(l, state);
    call_function(3, 0, "on_joyad_axis_moved");
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a joypad hat was just moved.
 * @param event The corresponding input event.
 */
void LuaContext::on_joypad_hat_moved(InputEvent& event) {

  if (find_method("on_joyad_hat_moved")) {
    int hat = event.get_joypad_hat();
    int direction8 = event.get_joypad_hat_direction();

    lua_pushinteger(l, hat);
    lua_pushinteger(l, direction8);
    call_function(3, 0, "on_joyad_hat_moved");
  }
}

/**
 * @brief Notifies the object on top of the stack
 * that a directional keyboard key was just pressed
 * or that a joypad directional command has just changed.
 * @param event The corresponding input event.
 */
void LuaContext::on_direction_pressed(InputEvent& event) {

  if (find_method("on_direction_pressed")) {
    int direction8 = event.get_direction();

    lua_pushinteger(l, direction8);
    call_function(2, 0, "on_direction_pressed");
  }
}

/**
 * @brief Calls the on_started() method of the object on top of the stack.
 * @param destination_point The destination point used (NULL if it's a special one).
 */
void LuaContext::on_started(DestinationPoint* destination_point) {

  if (find_method("on_started")) {
    if (destination_point == NULL) {
      lua_pushnil(l);
    }
    else {
      lua_pushstring(l, destination_point->get_name().c_str());
    }
    call_function(2, 0, "on_started");
  }
}

/**
 * @brief Calls the on_opening_transition_finished() method of the object on top of the stack.
 * @param destination_point The destination point used (NULL if it's a special one).
 */
void LuaContext::on_opening_transition_finished(DestinationPoint* destination_point) {

  if (find_method("on_opening_transition_finished")) {
    if (destination_point == NULL) {
      lua_pushnil(l);
    }
    else {
      lua_pushstring(l, destination_point->get_name().c_str());
    }
    call_function(2, 0, "on_opening_transition_finished");
  }
}

/**
 * @brief Calls the on_dialog_started() method of the object on top of the stack.
 * @param dialog_id Id of the dialog just started.
 */
void LuaContext::on_dialog_started(const std::string& dialog_id) {

  if (find_method("on_dialog_started")) {
    lua_pushstring(l, dialog_id.c_str());
    call_function(2, 0, "on_dialog_started");
  }
}

/**
 * @brief Calls the on_dialog_finished() method of the object on top of the stack.
 * @param dialog_id Id of the dialog just started.
 * @param answer The answer selected by the player: 0 for the first one,
 * 1 for the second one, -1 if there was no question.
 */
void LuaContext::on_dialog_finished(const std::string& dialog_id, int answer) {

  if (find_method("on_dialog_finished")) {
    lua_pushstring(l, dialog_id.c_str());
    lua_pushinteger(l, answer);
    call_function(3, 0, "on_dialog_finished");
  }
}

/**
 * @brief Calls the on_camera_back() method of the object on top of the stack.
 */
void LuaContext::on_camera_back() {

  if (find_method("on_camera_back")) {
    call_function(1, 0, "on_camera_back");
  }
}

/**
 * @brief Calls the on_treasure_obtaining() method of the object on top of the stack.
 * @param treasure The treasure being obtained.
 */
void LuaContext::on_treasure_obtaining(const Treasure& treasure) {

  if (find_method("on_treasure_obtaining")) {
    lua_pushstring(l, treasure.get_item_name().c_str());
    lua_pushinteger(l, treasure.get_variant());
    lua_pushinteger(l, treasure.get_savegame_variable());
    call_function(4, 0, "on_treasure_obtaining");
  }
}

/**
 * @brief Calls the on_treasure_obtained() method of the object on top of the stack.
 * @param treasure The treasure just obtained.
 */
void LuaContext::on_treasure_obtained(const Treasure& treasure) {

  if (find_method("on_treasure_obtained")) {
    lua_pushstring(l, treasure.get_item_name().c_str());
    lua_pushinteger(l, treasure.get_variant());
    lua_pushinteger(l, treasure.get_savegame_variable());
    call_function(4, 0, "on_treasure_obtained");
  }
}

/**
 * @brief Calls the on_switch_activated() method of the object on top of the stack.
 * @param sw A switch.
 */
void LuaContext::on_switch_activated(Switch& sw) {

  if (find_method("on_switch_activated")) {
    lua_pushstring(l, sw.get_name().c_str());
    call_function(2, 0, "on_switch_activated");
  }
}

/**
 * @brief Calls the on_switch_inactivated() method of the object on top of the stack.
 * @param sw A switch.
 */
void LuaContext::on_switch_inactivated(Switch& sw) {

  if (find_method("on_switch_inactivated")) {
    lua_pushstring(l, sw.get_name().c_str());
    call_function(2, 0, "on_switch_inactivated");
  }
}

/**
 * @brief Calls the on_switch_left() method of the object on top of the stack.
 * @param sw A switch.
 */
void LuaContext::on_switch_left(Switch& sw) {

  if (find_method("on_switch_left")) {
    lua_pushstring(l, sw.get_name().c_str());
    call_function(2, 0, "on_switch_left");
  }
}

/**
 * @brief Calls the on_hero_victory_sequence_finished() method of the object on top of the stack.
 */
void LuaContext::on_hero_victory_sequence_finished() {

  if (find_method("on_hero_victory_sequence_finished")) {
    call_function(1, 0, "on_hero_victory_sequence_finished");
  }
}

/**
 * @brief Calls the on_hero_on_sensor() method of the object on top of the stack.
 * @param sensor A sensor that just detected the hero.
 */
void LuaContext::on_hero_on_sensor(Sensor& sensor) {

  if (find_method("on_hero_on_sensor")) {
    lua_pushstring(l, sensor.get_name().c_str());
    call_function(2, 0, "on_hero_on_sensor");
  }
}

/**
 * @brief Calls the on_hero_still_on_sensor() method of the object on top of the stack.
 * @param sensor A sensor that just detected the hero.
 */
void LuaContext::on_hero_still_on_sensor(Sensor& sensor) {

  if (find_method("on_hero_still_on_sensor")) {
    lua_pushstring(l, sensor.get_name().c_str());
    call_function(2, 0, "on_hero_still_on_sensor");
  }
}

/**
 * @brief Calls the on_npc_movement_finished() method of the object on top of the stack.
 * @param npc An NPC.
 */
void LuaContext::on_npc_movement_finished(NPC& npc) {

  if (find_method("on_npc_movement_finished")) {
    lua_pushstring(l, npc.get_name().c_str());
    call_function(2, 0, "on_npc_movement_finished");
  }
}

/**
 * @brief Calls the on_npc_interaction() method of the object on top of the stack.
 * @param npc An NPC.
 */
void LuaContext::on_npc_interaction(NPC& npc) {

  if (find_method("on_npc_interaction")) {
    lua_pushstring(l, npc.get_name().c_str());
    call_function(2, 0, "on_npc_interaction");
  }
}

/**
 * @brief Calls the on_npc_interaction_finished() method of the object on top of the stack.
 * @param npc An NPC.
 */
void LuaContext::on_npc_interaction_finished(NPC& npc) {

  if (find_method("on_npc_interaction_finished")) {
    lua_pushstring(l, npc.get_name().c_str());
    call_function(2, 0, "on_npc_interaction_finished");
  }
}

/**
 * @brief Calls the on_npc_interaction_item() method of the object on top of the stack.
 * @param npc An NPC.
 * @return true if an interaction occurred.
 */
bool LuaContext::on_npc_interaction_item(NPC& npc,
    const std::string &item_name, int variant) {

  if (find_method("on_npc_interaction_item")) {
    lua_pushstring(l, npc.get_name().c_str());
    lua_pushstring(l, item_name.c_str());
    lua_pushinteger(l, variant);
    call_function(4, 1, "on_npc_interaction_item");
    return lua_toboolean(l, -1);
  }
  return false;
}

/**
 * @brief Calls the on_npc_interaction_item_finished() method of the object on top of the stack.
 * @param npc An NPC.
 */
void LuaContext::on_npc_interaction_item_finished(NPC& npc,
    const std::string &item_name, int variant) {

  if (find_method("on_npc_interaction_item_finished")) {
    lua_pushstring(l, npc.get_name().c_str());
    lua_pushstring(l, item_name.c_str());
    lua_pushinteger(l, variant);
    call_function(4, 0, "on_npc_interaction_item_finished");
  }
}

/**
 * @brief Calls the on_npc_collision_fire() method of the object on top of the stack.
 * @param npc An NPC.
 */
void LuaContext::on_npc_collision_fire(NPC& npc) {

  if (find_method("on_npc_collision_fire")) {
    lua_pushstring(l, npc.get_name().c_str());
    call_function(2, 0, "on_npc_collision_fire");
  }
}

/**
 * @brief Calls the on_sensor_collision_explosion() method of the object on top of the stack.
 * @param sensor A sensor.
 */
void LuaContext::on_sensor_collision_explosion(Sensor& sensor) {

  if (find_method("on_sensor_collision_explosion")) {
    lua_pushstring(l, sensor.get_name().c_str());
    call_function(2, 0, "on_sensor_collision_explosion");
  }
}

/**
 * @brief Calls the on_chest_empty() method of the object on top of the stack.
 * @param chest An empty chest.
 * @return true if the on_chest_empty() is defined.
 */
bool LuaContext::on_chest_empty(Chest& chest) {

  if (find_method("on_chest_empty")) {
    lua_pushstring(l, chest.get_name().c_str());
    call_function(2, 0, "on_chest_empty");
    return true;
  }
  return false;
}

/**
 * @brief Calls the on_shop_item_buying() method of the object on top of the stack.
 * @param shop_item Name of a shop item to buy.
 * @return true if the player is allowed to buy the item.
 */
bool LuaContext::on_shop_item_buying(ShopItem& shop_item) {

  if (find_method("on_shop_item_buying")) {
    lua_pushstring(l, shop_item.get_name().c_str());
    call_function(2, 1, "on_shop_item_buying");
    return lua_toboolean(l, -1);
  }
  return false;
}

/**
 * @brief Calls the on_shop_item_bought() method of the object on top of the stack.
 * @param shop_item Name of a shop item just bought.
 */
void LuaContext::on_shop_item_bought(ShopItem& shop_item) {

  if (find_method("on_shop_item_bought")) {
    lua_pushstring(l, shop_item.get_name().c_str());
    call_function(2, 0, "on_shop_item_bought");
  }
}

/**
 * @brief Calls the on_door_open() method of the object on top of the stack.
 * @param A door just opened.
 */
void LuaContext::on_door_open(Door& door) {

  if (find_method("on_door_open")) {
    lua_pushstring(l, door.get_name().c_str());
    call_function(2, 0, "on_door_open");
  }
}

/**
 * @brief Calls the on_() method of the object on top of the stack.
 * @param A door just closed.
 */
void LuaContext::on_door_closed(Door& door) {

  if (find_method("on_door_closed")) {
    lua_pushstring(l, door.get_name().c_str());
    call_function(2, 0, "on_door_closed");
  }
}

/**
 * @brief Calls the on_block_moved() method of the object on top of the stack.
 * @param block A block just moved.
 */
void LuaContext::on_block_moved(Block& block) {

  if (find_method("on_block_moved")) {
    lua_pushstring(l, block.get_name().c_str());
    call_function(2, 0, "on_block_moved");
  }
}

/**
 * @brief Calls the on_enemy_dying() method of the object on top of the stack.
 * @param enemy An enemy.
 */
void LuaContext::on_enemy_dying(Enemy& enemy) {

  if (find_method("on_enemy_dying")) {
    lua_pushstring(l, enemy.get_name().c_str());
    call_function(2, 0, "on_enemy_dying");
  }
}

/**
 * @brief Calls the on_enemy_dead() method of the object on top of the stack.
 * @param enemy An enemy.
 */
void LuaContext::on_enemy_dead(Enemy& enemy) {

  if (find_method("on_enemy_dead")) {
    lua_pushstring(l, enemy.get_name().c_str());
    call_function(2, 0, "on_enemy_dead");
  }
}

/**
 * @brief Calls the on_map_changed() method of the object on top of the stack.
 * @param map The new active map.
 */
void LuaContext::on_map_changed(Map& map) {

  if (find_method("on_map_changed")) {
    lua_pushinteger(l, map.get_id());
    call_function(2, 0, "on_map_changed");
  }
}

/**
 * @brief Calls the on_() method of the object on top of the stack.
 * @param pickable A pickable item.
 */
void LuaContext::on_appear(PickableItem& pickable) {

  if (find_method("on_appear")) {
    const Treasure& treasure = pickable.get_treasure();
    lua_pushinteger(l, treasure.get_variant());
    lua_pushinteger(l, treasure.get_savegame_variable());
    lua_pushinteger(l, pickable.get_falling_height());
    call_function(4, 0, "on_appear");
  }
}

/**
 * @brief Calls the on_movement_changed() method of the object on top of the stack.
 * @param pickable A pickable item.
 */
void LuaContext::on_movement_changed(PickableItem& pickable) {

  if (find_method("on_movement_changed")) {
    call_function(1, 0, "on_movement_changed");
  }
}

/**
 * @brief Calls the on_variant_changed() method of the object on top of the stack.
 * @param enemy Variant of an inventory item.
 */
void LuaContext::on_variant_changed(int variant) {

  if (find_method("on_variant_changed")) {
    lua_pushinteger(l, variant);
    call_function(2, 0, "on_variant_changed");
  }
}

/**
 * @brief Calls the on_amount_changed() method of the object on top of the stack.
 * @param amount Amount of an equipment item.
 */
void LuaContext::on_amount_changed(int amount) {

  if (find_method("on_amount_changed")) {
    lua_pushinteger(l, 1);
    call_function(2, 0, "on_amount_changed");
  }
}

/**
 * @brief Calls the on_obtaining() method of the object on top of the stack.
 * @param treasure The treasure being obtained.
 */
void LuaContext::on_obtaining(const Treasure& treasure) {

  if (find_method("on_obtaining")) {
    lua_pushinteger(l, treasure.get_variant());
    lua_pushinteger(l, treasure.get_savegame_variable());
    call_function(3, 0, "on_obtaining");
  }
}

/**
 * @brief Calls the on_obtained() method of the object on top of the stack.
 * @param treasure The treasure just obtained.
 */
void LuaContext::on_obtained(const Treasure& treasure) {

  if (find_method("on_obtained")) {
    lua_pushinteger(l, treasure.get_variant());
    lua_pushinteger(l, treasure.get_savegame_variable());
    call_function(3, 0, "on_obtained");
  }
}

/**
 * @brief Calls the on_use() method of the object on top of the stack.
 * @param enemy An enemy.
 */
void LuaContext::on_use(InventoryItem& inventory_item) {

  if (find_method("on_use")) {
    call_function(1, 0, "on_use");
  }
}

/**
 * @brief Calls the on_ability_used() method of the object on top of the stack.
 * @param ability_name Id of a built-in ability.
 */
void LuaContext::on_ability_used(const std::string& ability_name) {

  if (find_method("on_ability_used")) {
    lua_pushstring(l, ability_name.c_str());
    call_function(2, 0, "on_ability_used");
  }
}

/**
 * @brief Calls the on_appear() method of the object on top of the stack.
 */
void LuaContext::on_appear() {

  if (find_method("on_appear")) {
    call_function(1, 0, "on_appear");
  }
}

/**
 * @brief Calls the on_enabled() method of the object on top of the stack.
 */
void LuaContext::on_enabled() {

  if (find_method("on_enabled")) {
    call_function(1, 0, "on_enabled");
  }
}

/**
 * @brief Calls the on_disabled() method of the object on top of the stack.
 */
void LuaContext::on_disabled() {

  if (find_method("on_disabled")) {
    call_function(1, 0, "on_disabled");
  }
}

/**
 * @brief Calls the on_restart() method of the object on top of the stack.
 */
void LuaContext::on_restart() {

  if (find_method("on_restart")) {
    call_function(1, 0, "on_restart");
  }
}

/**
 * @brief Calls the on_pre_display() method of the object on top of the stack.
 */
void LuaContext::on_pre_display() {

  if (find_method("on_pre_display")) {
    call_function(1, 0, "on_pre_display");
  }
}

/**
 * @brief Calls the on_post_display() method of the object on top of the stack.
 */
void LuaContext::on_post_display() {

  if (find_method("on_post_display")) {
    call_function(1, 0, "on_post_display");
  }
}

/**
 * @brief Calls the on_position_changed() method of the object on top of the stack.
 * @param xy The new position.
 */
void LuaContext::on_position_changed(const Rectangle& xy) {

  if (find_method("on_position_changed")) {
    lua_pushinteger(l, xy.get_x());
    lua_pushinteger(l, xy.get_y());
    call_function(3, 0, "on_position_changed");
  }
}

/**
 * @brief Calls the on_layer_changed() method of the object on top of the stack.
 * @param layer The new layer.
 */
void LuaContext::on_layer_changed(Layer layer) {

  if (find_method("on_layer_changed")) {
    lua_pushinteger(l, layer);
    call_function(2, 0, "on_layer_changed");
  }
}

/**
 * @brief Calls the on_obstacle_reached() method of the object on top of the stack.
 */
void LuaContext::on_obstacle_reached() {

  if (find_method("on_obstacle_reached")) {
    call_function(1, 0, "on_obstacle_reached");
  }
}

/**
 * @brief Calls the on_movement_changed() method of the object on top of the stack.
 * @param movement A movement.
 */
void LuaContext::on_movement_changed(Movement& movement) {

  if (find_method("on_movement_changed")) {
    push_movement(l, movement);
    call_function(2, 0, "on_movement_changed");
  }
}

/**
 * @brief Calls the on_movement_finished() method of the object on top of the stack.
 * @param movement A movement.
 */
void LuaContext::on_movement_finished(Movement& movement) {

  if (find_method("on_movement_finished")) {
    push_movement(l, movement);
    call_function(2, 0, "on_movement_finished");
  }
}

/**
 * @brief Calls the on_sprite_animation_finished() method of the object on top of the stack.
 * @param sprite A sprite whose animation has just finished.
 * @param animation Name of the animation finished.
 */
void LuaContext::on_sprite_animation_finished(Sprite& sprite, const std::string& animation) {

  if (find_method("on_sprite_animation_finished")) {
    push_sprite(l, sprite);
    lua_pushstring(l, animation.c_str());
    call_function(3, 0, "on_sprite_animation_finished");
  }
}

/**
 * @brief Calls the on_sprite_frame_changed() method of the object on top of the stack.
 * @param sprite A sprite whose animation frame has just changed.
 * @param animation Name of the sprite animation.
 * @param frame The new frame.
 */
void LuaContext::on_sprite_frame_changed(Sprite& sprite, const std::string& animation, int frame) {

  if (find_method("on_sprite_frame_changed")) {
    push_sprite(l, sprite);
    lua_pushstring(l, animation.c_str());
    lua_pushinteger(l, frame);
    call_function(4, 0, "on_sprite_frame_changed");
  }
}

/**
 * @brief Calls the on_collision_enemy() method of the object on top of the stack.
 * @param other_enemy Another enemy colliding with the object on top of the stack.
 * @param other_sprite Colliding sprite of the other enemy.
 * @param this_sprite Colliding sprite of the first enemy.
 */
void LuaContext::on_collision_enemy(Enemy& other_enemy, Sprite& other_sprite, Sprite& this_sprite) {

  if (find_method("on_collision_enemy")) {
    lua_pushstring(l, other_enemy.get_name().c_str());
    push_sprite(l, other_sprite);
    push_sprite(l, this_sprite);
    call_function(4, 0, "on_collision_enemy");
  }
}

/**
 * @brief Calls the on_custom_attack_received() method of the object on top of the stack.
 * @param attack The attack received.
 * @param sprite The sprite that receives the attack if any.
 */
void LuaContext::on_custom_attack_received(EnemyAttack attack, Sprite* sprite) {

  if (find_method("on_custom_attack_received")) {
    lua_pushstring(l, Enemy::get_attack_name(attack).c_str());
    if (sprite != NULL) {
      // Pixel-precise collision.
      push_sprite(l, *sprite);
      call_function(3, 0, "on_custom_attack_received");
    }
    else {
      call_function(2, 0, "on_custom_attack_received");
    }
  }
}

/**
 * @brief Calls the on_hurt() method of the object on top of the stack.
 * @param attack The attack received.
 * @param life_lost Number of life points just lost.
 */
void LuaContext::on_hurt(EnemyAttack attack, int life_lost) {

  if (find_method("on_hurt")) {
    lua_pushstring(l, Enemy::get_attack_name(attack).c_str());
    lua_pushinteger(l, life_lost);
    call_function(3, 0, "on_hurt");
  }
}

/**
 * @brief Calls the on_dead() method of the object on top of the stack.
 */
void LuaContext::on_dead() {

  if (find_method("on_dead")) {
    call_function(1, 0, "on_dead");
  }
}

/**
 * @brief Calls the on_immobilized() method of the object on top of the stack.
 */
void LuaContext::on_immobilized() {

  if (find_method("on_immobilized")) {
    call_function(1, 0, "on_immobilized");
  }
}

/**
 * @brief Calls the on_message_received() method of the object on top of the stack.
 * @param src_enemy The sender.
 * @param message The message received.
 */
void LuaContext::on_message_received(Enemy& src_enemy, const std::string& message) {

  if (find_method("on_message_received")) {
    lua_pushstring(l, src_enemy.get_name().c_str());
    lua_pushstring(l, message.c_str());
    call_function(3, 0, "on_message_received");
  }
}
