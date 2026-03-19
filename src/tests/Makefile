CXX = g++
CXXFLAGS = -std=c++1z -Wall -Wextra -D TESTING
LDFLAGS = -lgtest

OBJFILES = act_comm.o act_info.o act_move.o act_obj1.o act_obj2.o act_offe.o \
	act_othe.o act_soci.o act_wiz.o ban.o battle_mage_handler.o big_brother.o boards.o char_utils.o char_utils_combat.o clerics.o clock.o color.o combat_manager.o \
	comm.o config.o consts.o db.o delayed_command_interpreter.o fight.o graph.o handler.o interpre.o environment_utils.o \
	limits.o mail.o mystic.o mage.o mobact.o modify.o mudlle.o mudlle2.o mob_csv_extract.o obj2html.o object_utils.o objsave.o olog_hai.o\
	pkill.o profs.o protocol.o ranger.o script.o shapemdl.o shapemob.o shapeobj.o shaperom.o shapezon.o shapescript.o shop.o \
	signals.o skill_timer.o spec_ass.o spec_pro.o spell_pa.o utility.o wait_functions.o weapon_master_handler.o  \
	wild_fighting_handler.o weather.o zone.o


# Dependencies for the main mud

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
ranger.o : ../ranger.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h\
	 ../spells.h ../script.h
	 $(CXX) -c $(CXXFLAGS) ../ranger.cpp
script.o : ../script.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../script.h
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


SRCS = CharPlayerDataBuilder.cpp ObjFlagDataBuilder.cpp obj_flag_data_tests.cpp gtest_main.cpp

OBJS = $(SRCS:.cpp=.o)
EXECUTABLE = ../../bin/tests

tests: $(EXECUTABLE)

$(EXECUTABLE): $(OBJFILES) $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJFILES) $(OBJS) -o $(EXECUTABLE) $(LDFLAGS)

.cpp.o:
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(EXECUTABLE)

ageland: ../bin/ageland

../bin/ageland: $(OBJFILES)

all: ageland $(EXECUTABLE)

run: $(EXECUTABLE)
	$(EXECUTABLE)
