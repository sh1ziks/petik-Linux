#include <utility>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <iostream>

class SocketHandle {
private:
	int fd_ = -1;

public:
	SocketHandle() = default;

	explicit SocketHandle(int fd) : fd_(fd) {}

	~SocketHandle(){
		reset();
	}

	SocketHandle(const SocketHandle&) = delete;
	SocketHandle& operator=(const SocketHandle&) = delete;

	SocketHandle(SocketHandle&& other)	noexcept : fd_(std::exchange(other.fd_, -1)) {}

	SocketHandle& operator=(SocketHandle&& other) noexcept {
		if (this == &other)
			return *this;
		
		reset(std::exchange(other.fd_, -1));
		return *this;
	}

	int get() const {
		return fd_;
	}

	bool valid() const{
		return fd_ != -1;
	}

    void reset(int new_fd = -1){
        if (fd_ != -1)
            ::close(fd_);

        fd_ = new_fd;
    }
};

SocketHandle create_listener(uint16_t port){
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		throw std::runtime_error(std::string("socket: ") + strerror(errno));
	SocketHandle sock(fd);
		
	int opt = 1;
	if (setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		throw std::runtime_error(std::string("setsockopt: ") + strerror(errno));
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	if (bind(sock.get(), (sockaddr*)&addr, sizeof(addr)) < 0)
		throw std::runtime_error(std::string("bind: ") + strerror(errno));
	if (listen(sock.get(), 128) < 0)
		throw std::runtime_error(std::string("listen: ") + strerror(errno));

	return sock;
}

void server_echo(SocketHandle client){
	char buf[1025];
	ssize_t n;
	while((n = recv(client.get(), buf, sizeof(buf), 0)) > 0)
	{
		buf[n - 1] = '!';
		buf[n] = '\n';
		send(client.get(), buf, n + 1, 0);
	}
}

int main(){
	try
	{
		auto listener = create_listener(8080);
		
		while (true)
		{
			int fd = ::accept(listener.get(), nullptr, nullptr);

			if (fd < 0)
			{
				perror("accept");
				continue;
			}

			server_echo(SocketHandle(fd));
		}
	} catch(const std::exception& e)
	{
		std::cerr <<"fatal: " << e.what() << "\n";
		return 1;
	}
}