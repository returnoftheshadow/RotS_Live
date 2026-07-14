CXX = g++
QJS_CC = gcc
CXXFLAGS = -std=c++1z -Wall -Wextra -D TESTING -I..
QJS_CFLAGS = -std=gnu11 -D_GNU_SOURCE -DCONFIG_VERSION=\"2026-06-04\" -I../../third_party/quickjs
QJS_HEADERS = ../../third_party/quickjs/quickjs.h ../../third_party/quickjs/quickjs-atom.h \
	../../third_party/quickjs/quickjs-opcode.h ../../third_party/quickjs/cutils.h \
	../../third_party/quickjs/list.h ../../third_party/quickjs/libregexp.h \
	../../third_party/quickjs/libregexp-opcode.h ../../third_party/quickjs/libunicode.h \
	../../third_party/quickjs/libunicode-table.h ../../third_party/quickjs/dtoa.h
LDFLAGS = -lgtest -lcrypt

OBJFILES = act_comm.o act_info.o act_move.o act_obj1.o act_obj2.o act_offe.o \
	act_othe.o act_soci.o act_wiz.o ban.o battle_mage_handler.o big_brother.o boards.o char_utils.o char_utils_combat.o clerics.o clock.o color.o combat_manager.o \
	comm.o config.o consts.o crashsave_schedule.o db.o delayed_command_interpreter.o fight.o graph.o handler.o interpre.o environment_utils.o account_management.o account_cache.o character_json.o exploits_json.o json_utils.o js_api_contract.o js_builder_artifacts.o js_game_adapter.o js_game_runtime.o js_legacy_trigger_dispatch.o js_live_package_store.o js_live_package_store_persistence.o js_live_registry_admin.o js_live_registry_reload_service.o js_live_registry_status.o js_manifest_export.o js_publish_activation.o js_publish_authorization.o js_publish_endpoint_contract.o js_publish_http_endpoint.o js_publish_endpoint_service.o js_publish_endpoint_transport.o js_publish_staging.o js_runtime.o js_staged_package_identity.o js_staged_package_repository.o js_script_package_loader.o js_script_package_reload_service.o js_script_package.o js_script_registry.o js_trigger_dispatch.o js_scripting_manifest.o objects_json.o \
	limits.o mail.o mystic.o mage.o mobact.o modify.o mudlle.o mudlle2.o mob_csv_extract.o obj2html.o object_utils.o objsave.o olog_hai.o\
	pkill.o player_file_finalize.o profs.o protocol.o ranger.o save_benchmark.o savebench.o script.o shapemdl.o shapemob.o shapeobj.o shaperom.o shapezon.o shapescript.o shop.o \
	signals.o skill_timer.o spec_ass.o spec_pro.o spell_pa.o utility.o wait_functions.o weapon_master_handler.o  \
	wild_fighting_handler.o weather.o zone.o quickjs.o cutils.o libregexp.o libunicode.o dtoa.o


# Dependencies for the main mud

account_management.o : ../account_management.cpp ../account_management.h ../account_management_types.h ../account_management_identity.h ../account_management_storage.h ../account_management_assets.h ../account_management_migration.h ../account_management_presentation.h ../account_management_internal.cpp ../account_management_identity.cpp ../account_management_storage.cpp ../account_management_assets.cpp ../account_management_migration.cpp ../account_management_presentation.cpp ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../account_management.cpp

account_cache.o : ../account_cache.cpp ../account_cache.h ../account_management.h
	$(CXX) -c $(CXXFLAGS) ../account_cache.cpp

character_json.o : ../character_json.cpp ../character_json.h ../json_utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../character_json.cpp

exploits_json.o : ../exploits_json.cpp ../exploits_json.h ../json_utils.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../exploits_json.cpp

json_utils.o : ../json_utils.cpp ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../json_utils.cpp

js_api_contract.o : ../js_api_contract.cpp ../js_api_contract.h
	$(CXX) -c $(CXXFLAGS) ../js_api_contract.cpp

js_builder_artifacts.o : ../js_builder_artifacts.cpp ../js_builder_artifacts.h ../js_api_contract.h ../js_manifest_export.h ../json_utils.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_builder_artifacts.cpp

js_game_adapter.o : ../js_game_adapter.cpp ../js_game_adapter.h ../js_game_runtime.h ../structs.h ../db.h ../zone.h
	$(CXX) -c $(CXXFLAGS) ../js_game_adapter.cpp

js_game_runtime.o : ../js_game_runtime.cpp ../js_game_runtime.h ../js_runtime.h
	$(CXX) -c $(CXXFLAGS) ../js_game_runtime.cpp

js_legacy_trigger_dispatch.o : ../js_legacy_trigger_dispatch.cpp ../js_legacy_trigger_dispatch.h ../js_trigger_dispatch.h ../js_game_adapter.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_game_runtime.h ../js_runtime.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_legacy_trigger_dispatch.cpp

js_live_package_store.o : ../js_live_package_store.cpp ../js_live_package_store.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_live_package_store.cpp

js_live_package_store_persistence.o : ../js_live_package_store_persistence.cpp ../js_live_package_store_persistence.h ../js_live_package_store.h ../json_utils.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_live_package_store_persistence.cpp

js_live_registry_admin.o : ../js_live_registry_admin.cpp ../js_live_registry_admin.h ../js_live_package_store.h ../js_live_registry_reload_service.h ../js_live_registry_status.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_live_registry_admin.cpp

js_live_registry_reload_service.o : ../js_live_registry_reload_service.cpp ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_live_registry_reload_service.cpp

js_live_registry_status.o : ../js_live_registry_status.cpp ../js_live_registry_status.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_live_registry_status.cpp

js_manifest_export.o : ../js_manifest_export.cpp ../js_manifest_export.h ../js_api_contract.h ../json_utils.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_manifest_export.cpp

js_publish_activation.o : ../js_publish_activation.cpp ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_activation.cpp

js_publish_authorization.o : ../js_publish_authorization.cpp ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_authorization.cpp

js_publish_endpoint_contract.o : ../js_publish_endpoint_contract.cpp ../js_publish_endpoint_contract.h ../json_utils.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_endpoint_contract.cpp

js_publish_http_endpoint.o : ../js_publish_http_endpoint.cpp ../js_publish_http_endpoint.h ../js_publish_endpoint_transport.h ../js_publish_endpoint_contract.h ../json_utils.h ../js_publish_endpoint_service.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_http_endpoint.cpp

js_publish_endpoint_service.o : ../js_publish_endpoint_service.cpp ../js_publish_endpoint_service.h ../js_publish_endpoint_contract.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_endpoint_service.cpp

js_publish_endpoint_transport.o : ../js_publish_endpoint_transport.cpp ../js_publish_endpoint_transport.h ../js_publish_endpoint_service.h ../js_publish_endpoint_contract.h ../js_script_package_loader.h ../json_utils.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_endpoint_transport.cpp

js_publish_staging.o : ../js_publish_staging.cpp ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_publish_staging.cpp

js_runtime.o : ../js_runtime.cpp ../js_runtime.h $(QJS_HEADERS)
	$(CXX) -c $(CXXFLAGS) ../js_runtime.cpp

js_staged_package_identity.o : ../js_staged_package_identity.cpp ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_staged_package_identity.cpp

js_staged_package_repository.o : ../js_staged_package_repository.cpp ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_staged_package_repository.cpp

js_script_package_loader.o : ../js_script_package_loader.cpp ../js_script_package_loader.h ../js_script_package.h ../js_script_registry.h ../json_utils.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_script_package_loader.cpp

js_script_package_reload_service.o : ../js_script_package_reload_service.cpp ../js_script_package_reload_service.h ../js_script_package_loader.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_script_package_reload_service.cpp

js_script_package.o : ../js_script_package.cpp ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_script_package.cpp

js_script_registry.o : ../js_script_registry.cpp ../js_script_registry.h ../js_script_package.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_script_registry.cpp

js_trigger_dispatch.o : ../js_trigger_dispatch.cpp ../js_trigger_dispatch.h ../js_game_adapter.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_game_runtime.h ../js_runtime.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h
	$(CXX) -c $(CXXFLAGS) ../js_trigger_dispatch.cpp

js_scripting_manifest.o : ../js_scripting_manifest.cpp ../js_scripting_manifest.h ../interpre.h ../script.h
	$(CXX) -c $(CXXFLAGS) ../js_scripting_manifest.cpp

quickjs.o : ../../third_party/quickjs/quickjs.c $(QJS_HEADERS)
	$(QJS_CC) -c $(QJS_CFLAGS) ../../third_party/quickjs/quickjs.c

cutils.o : ../../third_party/quickjs/cutils.c $(QJS_HEADERS)
	$(QJS_CC) -c $(QJS_CFLAGS) ../../third_party/quickjs/cutils.c

libregexp.o : ../../third_party/quickjs/libregexp.c $(QJS_HEADERS)
	$(QJS_CC) -c $(QJS_CFLAGS) ../../third_party/quickjs/libregexp.c

libunicode.o : ../../third_party/quickjs/libunicode.c $(QJS_HEADERS)
	$(QJS_CC) -c $(QJS_CFLAGS) ../../third_party/quickjs/libunicode.c

dtoa.o : ../../third_party/quickjs/dtoa.c $(QJS_HEADERS)
	$(QJS_CC) -c $(QJS_CFLAGS) ../../third_party/quickjs/dtoa.c

objects_json.o : ../objects_json.cpp ../objects_json.h ../json_utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../objects_json.cpp

clock.o : ../clock.cpp ../clock.h
	$(CXX) -c $(CXXFLAGS) ../clock.cpp

comm.o : ../comm.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h \
	../limits.h ../clock.h
	$(CXX) -c $(CXXFLAGS) $(COMMFLAGS) ../comm.cpp
char_utils.o : ../char_utils.cpp ../base_utils.h ../char_utils.h ../object_utils.h \
	../environment_utils.h ../structs.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../char_utils.cpp
environment_utils.o : ../environment_utils.cpp ../structs.h ../environment_utils.h
	$(CXX) -c $(CXXFLAGS) ../environment_utils.cpp
object_utils.o : ../object_utils.cpp ../structs.h ../object_utils.h
	$(CXX) -c $(CXXFLAGS) ../object_utils.cpp
char_utils_combat.o : ../char_utils_combat.cpp ../structs.h ../base_utils.h ../char_utils.h ../object_utils.h  ../comm.h ../char_utils_combat.h
	$(CXX) -c $(CXXFLAGS) ../char_utils_combat.cpp
combat_manager.o : ../combat_manager.cpp ../combat_manager.h ../base_utils.h
	$(CXX) -c $(CXXFLAGS) ../combat_manager.cpp
big_brother.o : ../big_brother.cpp ../big_brother.h ../base_utils.h ../singleton.h
	$(CXX) -c $(CXXFLAGS) ../big_brother.cpp
battle_mage_handler.o : ../battle_mage_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../battle_mage_handler.cpp
wild_fighting_handler.o : ../wild_fighting_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../wild_fighting_handler.cpp
weapon_master_handler.o : ../weapon_master_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../weapon_master_handler.cpp
skill_timer.o : ../skill_timer.cpp ../skill_timer.h ../singleton.h
	$(CXX) -c $(CXXFLAGS) ../skill_timer.cpp
olog_hai.o : ../olog_hai.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../olog_hai.cpp
delayed_command_interpreter.o : ../delayed_command_interpreter.cpp ../delayed_command_interpreter.h
	$(CXX) -c $(CXXFLAGS) ../delayed_command_interpreter.cpp
wait_functions.o : ../wait_functions.cpp ../structs.h ../base_utils.h ../char_utils.h ../object_utils.h  ../comm.h
	$(CXX) -c $(CXXFLAGS) ../wait_functions.cpp
act_comm.o : ../act_comm.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h
	$(CXX) -c $(CXXFLAGS) ../act_comm.cpp
act_info.o : ../act_info.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_info.cpp
act_move.o : ../act_move.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_move.cpp
act_obj1.o : ../act_obj1.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_obj1.cpp
act_obj2.o : ../act_obj2.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_obj2.cpp
act_offe.o : ../act_offe.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_offe.cpp
act_othe.o : ../act_othe.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_othe.cpp
act_soci.o : ../act_soci.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_soci.cpp
act_wiz.o : ../act_wiz.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h ../profs.h
	$(CXX) -c $(CXXFLAGS) ../act_wiz.cpp
handler.o : ../handler.cpp ../structs.h ../utils.h ../comm.h ../db.h ../handler.h ../interpre.h
	$(CXX) -c $(CXXFLAGS) ../handler.cpp
db.o : ../db.cpp ../structs.h ../utils.h ../db.h ../comm.h ../handler.h ../limits.h ../spells.h \
        ../interpre.h ../big_brother.h ../skill_timer.h
	$(CXX) -c $(CXXFLAGS) ../db.cpp
ban.o : ../ban.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../ban.cpp
interpre.o : ../interpre.cpp ../structs.h ../comm.h ../interpre.h ../db.h ../utils.h \
	../limits.h ../spells.h ../handler.h ../profs.h
	$(CXX) -c $(CXXFLAGS) ../interpre.cpp
utility.o : ../utility.cpp ../structs.h ../utils.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../utility.cpp
spec_ass.o : ../spec_ass.cpp ../structs.h ../db.h ../interpre.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../spec_ass.cpp
spec_pro.o : ../spec_pro.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../spec_pro.cpp
limits.o : ../limits.cpp ../structs.h ../limits.h ../utils.h ../spells.h ../comm.h ../db.h ../handler.h          ../profs.h
	$(CXX) -c $(CXXFLAGS) ../limits.cpp
fight.o	: ../fight.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../fight.cpp
weather.o : ../weather.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../weather.cpp
shop.o : ../shop.cpp ../structs.h ../comm.h ../handler.h ../db.h ../interpre.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../shop.cpp
mystic.o : ../mystic.cpp ../structs.h ../utils.h ../comm.h ../spells.h ../handler.h ../limits.h \
	../interpre.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../mystic.cpp
mage.o  : ../mage.cpp ../structs.h ../utils.h ../comm.h ../spells.h ../handler.h ../limits.h \
        ../interpre.h ../db.h
	${CXX} -c ${CXXFLAGS} ../mage.cpp
spell_pa.o : ../spell_pa.cpp ../structs.h ../utils.h ../comm.h ../db.h ../interpre.h \
	../spells.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../spell_pa.cpp
mobact.o : ../mobact.cpp ../utils.h ../structs.h ../db.h ../comm.h ../interpre.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../mobact.cpp
modify.o : ../modify.cpp ../structs.h ../utils.h ../interpre.h ../handler.h ../db.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../modify.cpp
consts.o : ../consts.cpp ../structs.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../consts.cpp
objsave.o : ../objsave.cpp ../structs.h ../comm.h ../handler.h ../db.h ../interpre.h \
	../utils.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../objsave.cpp
boards.o : ../boards.cpp ../structs.h ../utils.h ../comm.h ../db.h ../boards.h ../interpre.h \
	../handler.h
	$(CXX) -c $(CXXFLAGS) ../boards.cpp
signals.o : ../signals.cpp ../utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../signals.cpp
graph.o : ../graph.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h \
	../spells.h
	$(CXX) -c $(CXXFLAGS) ../graph.cpp
config.o : ../config.cpp ../structs.h
	$(CXX) -c $(CXXFLAGS) ../config.cpp
crashsave_schedule.o : ../crashsave_schedule.cpp ../crashsave_schedule.h
	$(CXX) -c $(CXXFLAGS) ../crashsave_schedule.cpp
ranger.o : ../ranger.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h\
	 ../spells.h ../script.h
	 $(CXX) -c $(CXXFLAGS) ../ranger.cpp
player_file_finalize.o : ../player_file_finalize.cpp ../player_file_finalize.h
	$(CXX) -c $(CXXFLAGS) ../player_file_finalize.cpp
save_benchmark.o : ../save_benchmark.cpp ../save_benchmark.h ../account_cache.h ../account_management.h ../character_json.h ../db.h ../stopwatch.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../save_benchmark.cpp
savebench.o : ../savebench.cpp ../savebench.h ../save_benchmark.h ../account_management.h ../comm.h ../db.h ../structs.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../savebench.cpp
script.o : ../script.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../script.h ../js_legacy_trigger_dispatch.h ../js_live_registry_admin.h ../js_game_adapter.h ../js_live_registry_reload_service.h
	$(CXX) -c $(CXXFLAGS) ../script.cpp
shapemob.o : ../shapemob.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapemob.cpp
shapeobj.o : ../shapeobj.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapeobj.cpp
shaperom.o : ../shaperom.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shaperom.cpp
shapezon.o : ../shapezon.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapezon.cpp
shapemdl.o : ../shapemdl.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapemdl.cpp
shapescript.o : ../shapescript.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapescript.cpp
mudlle.o   : ../mudlle.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../mudlle.h
	$(CXX) -c $(CXXFLAGS) ../mudlle.cpp
mudlle2.o   : ../mudlle2.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../mudlle.h
	$(CXX) -c $(CXXFLAGS) ../mudlle2.cpp
profs.o   : ../profs.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../limits.h ../profs.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../profs.cpp
clerics.o : ../clerics.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../clerics.cpp
mail.o    : ../mail.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../db.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../mail.cpp
zone.o: ../zone.cpp ../zone.h ../structs.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../zone.cpp
color.o: ../color.cpp ../color.h
	$(CXX) -c $(CXXFLAGS) ../color.cpp
obj2html.o: ../obj2html.cpp
	$(CXX) -c $(CXXFLAGS) ../obj2html.cpp
mob_csv_extract.o: ../mob_csv_extract.cpp ../mob_csv_extract.h
	$(CXX) -c $(CXXFLAGS) ../mob_csv_extract.cpp
pkill.o: ../pkill.cpp ../pkill.h
	$(CXX) -c $(CXXFLAGS) ../pkill.cpp
protocol.o: ../protocol.cpp ../protocol.h
	$(CXX) -c $(CXXFLAGS) ../protocol.cpp


SRCS = account_management_tests.cpp account_management_assets_header_compile.cpp account_management_identity_header_compile.cpp account_management_migration_header_compile.cpp account_management_presentation_header_compile.cpp account_management_storage_header_compile.cpp account_management_types_header_compile.cpp act_wiz_tests.cpp battle_mage_handler_tests.cpp CharPlayerDataBuilder.cpp character_json_tests.cpp char_utils_tests.cpp damage_tests.cpp db_loader_tests.cpp exploits_json_tests.cpp fight_proc_tests.cpp interpre_account_menu_tests.cpp json_utils_tests.cpp js_api_contract_tests.cpp js_builder_artifacts_tests.cpp js_game_adapter_tests.cpp js_game_runtime_tests.cpp js_legacy_trigger_dispatch_tests.cpp js_live_package_store_tests.cpp js_live_package_store_persistence_tests.cpp js_live_registry_admin_tests.cpp js_live_registry_reload_service_tests.cpp js_live_registry_status_tests.cpp js_manifest_export_tests.cpp js_publish_activation_tests.cpp js_publish_authorization_tests.cpp js_publish_endpoint_contract_tests.cpp js_publish_http_endpoint_tests.cpp js_publish_endpoint_service_tests.cpp js_publish_endpoint_transport_tests.cpp js_publish_staging_tests.cpp js_runtime_tests.cpp js_staged_package_identity_tests.cpp js_staged_package_repository_tests.cpp js_script_package_loader_tests.cpp js_script_package_reload_service_tests.cpp js_script_package_tests.cpp js_script_registry_tests.cpp js_trigger_dispatch_tests.cpp js_scripting_manifest_tests.cpp mage_tests.cpp ObjFlagDataBuilder.cpp objects_json_tests.cpp olog_hai_tests.cpp obj_flag_data_tests.cpp object_utils_tests.cpp protocol_tests.cpp startup_options_tests.cpp test_random_utils.cpp test_random_utils_tests.cpp weapon_master_handler_tests.cpp gtest_main.cpp

OBJS = $(SRCS:.cpp=.o)
EXECUTABLE = ../../bin/tests

js_api_contract_tests.o : js_api_contract_tests.cpp ../js_api_contract.h ../js_scripting_manifest.h
js_builder_artifacts_tests.o : js_builder_artifacts_tests.cpp ../js_builder_artifacts.h ../js_api_contract.h ../json_utils.h ../js_scripting_manifest.h
js_game_adapter_tests.o : js_game_adapter_tests.cpp ../js_game_adapter.h ../js_game_runtime.h ../structs.h ../db.h ../zone.h
js_game_runtime_tests.o : js_game_runtime_tests.cpp ../js_game_runtime.h ../js_runtime.h
js_legacy_trigger_dispatch_tests.o : js_legacy_trigger_dispatch_tests.cpp ../js_legacy_trigger_dispatch.h ../js_trigger_dispatch.h ../js_game_adapter.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_live_registry_admin.h ../js_game_runtime.h ../js_runtime.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h ../protos.h ../structs.h ../db.h ../zone.h
js_live_package_store_tests.o : js_live_package_store_tests.cpp ../js_live_package_store.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_live_package_store_persistence_tests.o : js_live_package_store_persistence_tests.cpp ../js_live_package_store_persistence.h ../js_live_package_store.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_live_registry_admin_tests.o : js_live_registry_admin_tests.cpp ../js_live_registry_admin.h ../js_live_package_store.h ../js_live_registry_reload_service.h ../js_live_registry_status.h ../js_publish_activation.h ../js_publish_staging.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_live_registry_reload_service_tests.o : js_live_registry_reload_service_tests.cpp ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_publish_activation.h ../js_publish_staging.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_live_registry_status_tests.o : js_live_registry_status_tests.cpp ../js_live_registry_status.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_publish_activation.h ../js_publish_staging.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_manifest_export_tests.o : js_manifest_export_tests.cpp ../js_manifest_export.h ../js_api_contract.h ../json_utils.h ../js_scripting_manifest.h
js_publish_activation_tests.o : js_publish_activation_tests.cpp ../js_publish_activation.h ../js_live_package_store.h ../js_publish_staging.h ../js_script_registry.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_authorization_tests.o : js_publish_authorization_tests.cpp ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_endpoint_contract_tests.o : js_publish_endpoint_contract_tests.cpp ../js_publish_endpoint_contract.h ../json_utils.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_http_endpoint_tests.o : js_publish_http_endpoint_tests.cpp ../js_publish_http_endpoint.h ../js_publish_endpoint_transport.h ../js_publish_endpoint_service.h ../js_publish_endpoint_contract.h ../json_utils.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package_loader.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_endpoint_service_tests.o : js_publish_endpoint_service_tests.cpp ../js_publish_endpoint_service.h ../js_publish_endpoint_contract.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_endpoint_transport_tests.o : js_publish_endpoint_transport_tests.cpp ../js_publish_endpoint_transport.h ../js_publish_endpoint_service.h ../js_publish_endpoint_contract.h ../json_utils.h ../js_publish_activation.h ../js_live_package_store.h ../js_live_package_store_persistence.h ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_publish_staging_tests.o : js_publish_staging_tests.cpp ../js_publish_staging.h ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_runtime_tests.o : js_runtime_tests.cpp ../js_runtime.h
js_staged_package_identity_tests.o : js_staged_package_identity_tests.cpp ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_staged_package_repository_tests.o : js_staged_package_repository_tests.cpp ../js_staged_package_repository.h ../js_staged_package_identity.h ../js_publish_authorization.h ../js_script_package.h ../js_scripting_manifest.h
js_script_package_loader_tests.o : js_script_package_loader_tests.cpp ../js_script_package_loader.h ../js_script_package.h ../js_script_registry.h ../json_utils.h ../js_scripting_manifest.h
js_script_package_reload_service_tests.o : js_script_package_reload_service_tests.cpp ../js_script_package_reload_service.h ../js_script_package_loader.h ../js_script_package.h ../js_script_registry.h ../json_utils.h ../js_scripting_manifest.h
js_script_package_tests.o : js_script_package_tests.cpp ../js_script_package.h ../js_scripting_manifest.h
js_script_registry_tests.o : js_script_registry_tests.cpp ../js_script_registry.h ../js_script_package.h ../js_scripting_manifest.h
js_trigger_dispatch_tests.o : js_trigger_dispatch_tests.cpp ../js_trigger_dispatch.h ../js_game_adapter.h ../js_live_registry_reload_service.h ../js_live_package_store.h ../js_game_runtime.h ../js_runtime.h ../js_script_package.h ../js_script_registry.h ../js_scripting_manifest.h ../structs.h ../db.h ../zone.h
js_scripting_manifest_tests.o : js_scripting_manifest_tests.cpp ../js_scripting_manifest.h

tests: $(EXECUTABLE)

$(EXECUTABLE): $(OBJFILES) $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJFILES) $(OBJS) -o $(EXECUTABLE) $(LDFLAGS) -Wl,--wrap=_Z6numberv -Wl,--wrap=_Z6numberii

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(EXECUTABLE)

ageland: ../bin/ageland

../bin/ageland: $(OBJFILES)

all: ageland $(EXECUTABLE)

run: $(EXECUTABLE)
	$(EXECUTABLE)
