CC= clang
CFLAGS = -Wall -Wpedantic -Wextra -Werror 

PROG1 = fatReader32
SRCS1 = fatReader32.c

all: $(PROG1)
p1: $(PROG1)


$(PROG1): $(SRCS1)
	$(CC)  $(CFLAGS) -o $(PROG1) $(SRCS1)
	
	
clean:
	rm -f $(PROG1)

