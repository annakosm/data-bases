# Paths
INCLUDE = ./include/
LIBRARY = ./lib/
BUILD = ./build/

DB = *.db

# Object Files
OBJ = $(BUILD)bf_main $(BUILD)hp_main


# Compiled
hp:
	@echo " Compile hp_main ...";
	gcc -I $(INCLUDE) -L $(LIBRARY) -Wl,-rpath,$(LIBRARY) ./examples/hp_main.c ./src/record.c ./src/hp_file.c -lbf -o $(BUILD)hp_main -O2

bf:
	@echo " Compile bf_main ...";
	gcc -I $(INCLUDE) -L $(LIBRARY) -Wl,-rpath,$(LIBRARY) ./examples/bf_main.c ./src/record.c -lbf -o $(BUILD)bf_main -O2;

ht:
	@echo " Compile hp_main ...";
	gcc -I $(INCLUDE) -L $(LIBRARY) -Wl,-rpath,$(LIBRARY) ./examples/ht_main.c ./src/record.c ./src/ht_table.c -lbf -o $(BUILD)ht_main -O2

# sht:
# 	@echo " Compile hp_main ...";
# 	gcc -I ./include/ -L ./lib/ -Wl,-rpath,./lib/ ./examples/sht_main.c ./src/record.c ./src/sht_table.c ./src/ht_table.c -lbf -o ./build/sht_main -O2

sht:
	@echo " Compile hp_main ...";
	gcc -I $(INCLUDE) -L $(LIBRARY) -Wl,-rpath,$(LIBRARY) ./examples/sht_main.c ./src/record.c ./src/sht_table.c ./src/ht_table.c -lbf -o ./build/sht_main -O2


# Run
runbf:
	@echo "Running bf:"
	$(BUILD)bf_main

runhp:
	@echo "Running hp:"
	# $(BUILD) hp_main
	$(BUILD)hp_main

runht:
	@echo "Running ht:"
	$(BUILD)ht_main

runsht:	
	@echo "Running sht:"
	$(BUILD)sht_main


# Clean
clean: 
	@echo "Clean previous db files..."
	rm -f $(DB)
	@echo "Cleaning..."
	rm -f $(OBJ)