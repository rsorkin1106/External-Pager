CC=g++ -g -Wall -fno-builtin -std=c++17

# List of source files for your pager
PAGER_SOURCES=vm_pager.cpp

# Generate the names of the pager's object files
PAGER_OBJS=${PAGER_SOURCES:.cpp=.o}

TEST_SOURCES = $(wildcard test*.cpp)
TESTS = $(TEST_SOURCES:%.cpp=%.exe)

all: pager app

# Compile the pager and tag this compilation
pager: ${PAGER_OBJS} libvm_pager.o
	${CC} -o $@ $^


test%: test%.cpp libvm_app.o
	${CC} -o $@ $^ -ldl

# Generic rules for compiling a source file to an object file
%.o: %.cpp
	${CC} -c $<
%.o: %.cc
	${CC} -c $<

clean:
	rm -f ${PAGER_OBJS} pager
	rm -f ${TESTS}
