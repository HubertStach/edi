CXX = g++
CXXFLAGS = -Wall -std=c++17

#nazwa programu
TARGET = edi
SRC = main.cpp
INSTALL_DIR = /usr/local/bin

all: $(TARGET)

comp:
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	@echo "Installing $(TARGET) text editor..."
	cp $(TARGET) $(INSTALL_DIR)
	chmod 755 $(INSTALL_DIR)/$(TARGET)
	@echo "Finished installing $(TARGET)"

unistall:
	@echo "Uninstalling $(TARGET)"
	rm -f $(INSTALL_DIRECTORY)/$(TARGET)
	@echo "Done, goodbye!"

clean:
	rm -f $(TARGET)

.phony: all install unistall clean
