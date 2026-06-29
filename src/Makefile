#compiler name
CC = g++ -m32 -w -std=c++1z

#compiler flags you want to use (other than profiling, such as -Wall)
MYFLAGS = -fstrict-aliasing -funsigned-char

#flags for profiling (see hacker.doc for more information)
PROFILE = -g -rdynamic

#remove the hash marks below if compiling under AIX
#CC = g++
#COMMFLAGS = -D_BSD

#remove the hash mark below if compiling under SVR4
#OSFLAGS = -DSVR4
#LIBS = -lsocket -lnsl

#remove the has mark below if compiling under IRIX
#LIBS = -lmalloc

#############################################################################

CFLAGS = $(MYFLAGS) $(PROFILE) $(OSFLAGS)

OBJFILES = act_comm.o act_info.o act_move.o act_obj1.o act_obj2.o act_offe.o \
	act_othe.o act_soci.o act_wiz.o ban.o battle_mage_handler.o big_brother.o boards.o char_utils.o char_utils_combat.o clerics.o clock.o color.o combat_manager.o \
	comm.o config.o consts.o db.o delayed_command_interpreter.o fight.o graph.o handler.o interpre.o environment_utils.o \
	limits.o mail.o mystic.o mage.o mobact.o modify.o mudlle.o mudlle2.o mob_csv_extract.o obj2html.o object_utils.o objsave.o olog_hai.o\
	pkill.o profs.o protocol.o ranger.o script.o shapemdl.o shapemob.o shapeobj.o shaperom.o shapezon.o shapescript.o shop.o \
	signals.o skill_timer.o spec_ass.o spec_pro.o spell_pa.o utility.o wait_functions.o weapon_master_handler.o  \
	wild_fighting_handler.o weather.o zone.o crashsave_schedule.o


ageland:        ../bin/ageland

../bin/ageland:	$(OBJFILES)

all:	ageland

clean:
	rm -f *.o

# Dependencies for the main mud

clock.o : clock.cpp clock.h
	$(CC) -c $(CFLAGS) clock.cpp

crashsave_schedule.o : crashsave_schedule.cpp crashsave_schedule.h
	$(CC) -c $(CFLAGS) crashsave_schedule.cpp

comm.o : comm.cpp structs.h utils.h comm.h interpre.h handler.h db.h \
	limits.h clock.h protocol.h
	$(CC) -c $(CFLAGS) $(COMMFLAGS) comm.cpp
char_utils.o : char_utils.cpp base_utils.h char_utils.h object_utils.h \
	environment_utils.h structs.h handler.h
	$(CC) -c $(CFLAGS) char_utils.cpp
environment_utils.o : environment_utils.cpp structs.h environment_utils.h
	$(CC) -c $(CFLAGS) environment_utils.cpp
object_utils.o : object_utils.cpp structs.h object_utils.h
	$(CC) -c $(CFLAGS) object_utils.cpp
char_utils_combat.o : char_utils_combat.cpp structs.h base_utils.h char_utils.h object_utils.h  comm.h char_utils_combat.h
	$(CC) -c $(CFLAGS) char_utils_combat.cpp
combat_manager.o : combat_manager.cpp combat_manager.h base_utils.h
	$(CC) -c $(CFLAGS) combat_manager.cpp
big_brother.o : big_brother.cpp big_brother.h base_utils.h singleton.h
	$(CC) -c $(CFLAGS) big_brother.cpp
battle_mage_handler.o : battle_mage_handler.cpp warrior_spec_handlers.h structs.h
	$(CC) -c $(CFLAGS) battle_mage_handler.cpp
wild_fighting_handler.o : wild_fighting_handler.cpp warrior_spec_handlers.h structs.h
	$(CC) -c $(CFLAGS) wild_fighting_handler.cpp
weapon_master_handler.o : weapon_master_handler.cpp warrior_spec_handlers.h structs.h
	$(CC) -c $(CFLAGS) weapon_master_handler.cpp
skill_timer.o : skill_timer.cpp skill_timer.h singleton.h
	$(CC) -c $(CFLAGS) skill_timer.cpp
olog_hai.o : olog_hai.cpp structs.h utils.h comm.h interpre.h handler.h \
	db.h spells.h limits.h 
	$(CC) -c $(CFLAGS) olog_hai.cpp
delayed_command_interpreter.o : delayed_command_interpreter.cpp delayed_command_interpreter.h
	$(CC) -c $(CFLAGS) delayed_command_interpreter.cpp
wait_functions.o : wait_functions.cpp structs.h base_utils.h char_utils.h object_utils.h  comm.h
	$(CC) -c $(CFLAGS) wait_functions.cpp
act_comm.o : act_comm.cpp structs.h utils.h comm.h interpre.h handler.h \
	db.h
	$(CC) -c $(CFLAGS) act_comm.cpp
act_info.o : act_info.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h limits.h
	$(CC) -c $(CFLAGS) act_info.cpp
act_move.o : act_move.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h
	$(CC) -c $(CFLAGS) act_move.cpp
act_obj1.o : act_obj1.cpp structs.h utils.h comm.h interpre.h handler.h \
	db.h spells.h
	$(CC) -c $(CFLAGS) act_obj1.cpp
act_obj2.o : act_obj2.cpp structs.h utils.h comm.h interpre.h handler.h \
	db.h spells.h limits.h
	$(CC) -c $(CFLAGS) act_obj2.cpp
act_offe.o : act_offe.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h limits.h
	$(CC) -c $(CFLAGS) act_offe.cpp
act_othe.o : act_othe.cpp structs.h utils.h comm.h interpre.h handler.h \
	db.h spells.h limits.h
	$(CC) -c $(CFLAGS) act_othe.cpp
act_soci.o : act_soci.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h
	$(CC) -c $(CFLAGS) act_soci.cpp
act_wiz.o : act_wiz.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h limits.h profs.h stopwatch.h
	$(CC) -c $(CFLAGS) act_wiz.cpp
handler.o : handler.cpp structs.h utils.h comm.h db.h handler.h interpre.h
	$(CC) -c $(CFLAGS) handler.cpp
db.o : db.cpp structs.h utils.h db.h comm.h handler.h limits.h spells.h \
        interpre.h big_brother.h skill_timer.h
	$(CC) -c $(CFLAGS) db.cpp
ban.o : ban.cpp structs.h utils.h comm.h interpre.h handler.h db.h
	$(CC) -c $(CFLAGS) ban.cpp
interpre.o : interpre.cpp structs.h comm.h interpre.h db.h utils.h \
	limits.h spells.h handler.h profs.h mob_csv_extract.h
	$(CC) -c $(CFLAGS) interpre.cpp
utility.o : utility.cpp structs.h utils.h comm.h
	$(CC) -c $(CFLAGS) utility.cpp
spec_ass.o : spec_ass.cpp structs.h db.h interpre.h utils.h
	$(CC) -c $(CFLAGS) spec_ass.cpp
spec_pro.o : spec_pro.cpp structs.h utils.h comm.h interpre.h \
	handler.h db.h spells.h limits.h
	$(CC) -c $(CFLAGS) spec_pro.cpp
limits.o : limits.cpp structs.h limits.h utils.h spells.h comm.h db.h handler.h          profs.h
	$(CC) -c $(CFLAGS) limits.cpp
fight.o	: fight.cpp structs.h utils.h comm.h handler.h interpre.h db.h spells.h limits.h
	$(CC) -c $(CFLAGS) fight.cpp
weather.o : weather.cpp structs.h utils.h comm.h handler.h interpre.h db.h
	$(CC) -c $(CFLAGS) weather.cpp
shop.o : shop.cpp structs.h comm.h handler.h db.h interpre.h utils.h
	$(CC) -c $(CFLAGS) shop.cpp
mystic.o : mystic.cpp structs.h utils.h comm.h spells.h handler.h limits.h \
	interpre.h db.h
	$(CC) -c $(CFLAGS) mystic.cpp
mage.o  : mage.cpp structs.h utils.h comm.h spells.h handler.h limits.h \
        interpre.h db.h
	${CC} -c ${CFLAGS} mage.cpp
spell_pa.o : spell_pa.cpp structs.h utils.h comm.h db.h interpre.h \
	spells.h handler.h
	$(CC) -c $(CFLAGS) spell_pa.cpp
mobact.o : mobact.cpp utils.h structs.h db.h comm.h interpre.h handler.h
	$(CC) -c $(CFLAGS) mobact.cpp
modify.o : modify.cpp structs.h utils.h interpre.h handler.h db.h comm.h
	$(CC) -c $(CFLAGS) modify.cpp
consts.o : consts.cpp structs.h limits.h
	$(CC) -c $(CFLAGS) consts.cpp
objsave.o : objsave.cpp structs.h comm.h handler.h db.h interpre.h \
	utils.h spells.h
	$(CC) -c $(CFLAGS) objsave.cpp
boards.o : boards.cpp structs.h utils.h comm.h db.h boards.h interpre.h \
	handler.h
	$(CC) -c $(CFLAGS) boards.cpp
signals.o : signals.cpp utils.h structs.h
	$(CC) -c $(CFLAGS) signals.cpp
graph.o : graph.cpp structs.h utils.h comm.h interpre.h handler.h db.h \
	spells.h
	$(CC) -c $(CFLAGS) graph.cpp
config.o : config.cpp structs.h
	$(CC) -c $(CFLAGS) config.cpp
ranger.o : ranger.cpp structs.h utils.h comm.h interpre.h handler.h db.h\
	 spells.h script.h
	 $(CC) -c $(CFLAGS) ranger.cpp
script.o : script.cpp structs.h utils.h comm.h interpre.h protos.h script.h
	$(CC) -c $(CFLAGS) script.cpp
shapemob.o : shapemob.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shapemob.cpp
shapeobj.o : shapeobj.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shapeobj.cpp
shaperom.o : shaperom.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shaperom.cpp
shapezon.o : shapezon.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shapezon.cpp
shapemdl.o : shapemdl.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shapemdl.cpp
shapescript.o : shapescript.cpp structs.h utils.h comm.h interpre.h protos.h
	$(CC) -c $(CFLAGS) shapescript.cpp
mudlle.o   : mudlle.cpp structs.h utils.h comm.h interpre.h protos.h mudlle.h
	$(CC) -c $(CFLAGS) mudlle.cpp
mudlle2.o   : mudlle2.cpp structs.h utils.h comm.h interpre.h protos.h mudlle.h
	$(CC) -c $(CFLAGS) mudlle2.cpp
profs.o   : profs.cpp structs.h utils.h comm.h interpre.h limits.h profs.h comm.h
	$(CC) -c $(CFLAGS) profs.cpp
clerics.o : clerics.cpp structs.h utils.h comm.h handler.h interpre.h db.h spells.h limits.h
	$(CC) -c $(CFLAGS) clerics.cpp
mail.o    : mail.cpp structs.h utils.h comm.h interpre.h db.h handler.h
	$(CC) -c $(CFLAGS) mail.cpp
zone.o: zone.cpp zone.h structs.h utils.h
	$(CC) -c $(CFLAGS) zone.cpp
color.o: color.cpp color.h
	$(CC) -c $(CFLAGS) color.cpp
obj2html.o: obj2html.cpp
	$(CC) -c $(CFLAGS) obj2html.cpp
mob_csv_extract.o: mob_csv_extract.cpp mob_csv_extract.h
	$(CC) -c $(CFLAGS) mob_csv_extract.cpp
pkill.o: pkill.cpp pkill.h
	$(CC) -c $(CFLAGS) pkill.cpp
protocol.o: protocol.cpp protocol.h
	$(CC) -c $(CFLAGS) protocol.cpp

.PHONY: format
format:
	clang-format -i -style=WebKit *.cpp
	clang-format -i -style=WebKit *.h

.PHONY: setup
setup:
	@mkdir -p ../bin ../log ../lib/misc \
		../lib/players/A-E \
		../lib/players/F-J \
		../lib/players/K-O \
		../lib/players/P-T \
		../lib/players/U-Z \
		../lib/players/ZZZ \
		../lib/plrobjs/A-E \
		../lib/plrobjs/F-J \
		../lib/plrobjs/K-O \
		../lib/plrobjs/P-T \
		../lib/plrobjs/U-Z \
		../lib/plrobjs/ZZZ \
		../lib/exploits/A-E \
		../lib/exploits/F-J \
		../lib/exploits/K-O \
		../lib/exploits/P-T \
		../lib/exploits/U-Z \
		../lib/exploits/ZZZ \
		../mobs
	@touch \
		../log/lastdeath \
		../lib/last_cmds \
		../lib/misc/pklist

.PHONY: run
run: ../bin/ageland setup
	@cd .. && ./bin/ageland -p

../bin/ageland:
	if [ -f ../bin/ageland ]; \
	  then mv -f ../bin/ageland ../bin/ageland~; \
	fi
	$(CC) -o ../bin/ageland $(PROFILE) $(OBJFILES) $(LIBS)
