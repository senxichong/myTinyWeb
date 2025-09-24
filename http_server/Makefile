CXX = g++
CXXFLAGS = -std=c++11 -Wall -O2
TARGET = http_server
SRCS = main.cpp http_conn.cpp utils.cpp
OBJS = $(SRCS:.cpp=.o)
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
clean:
	rm -f $(OBJS) $(TARGET)