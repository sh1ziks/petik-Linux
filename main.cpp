#include <utility>
#include <unistd.h>
#include <string>
#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <iostream>
#include <csignal>
#include <cerrno>
#include <cstdio>

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

class LineReader {
private:
	int fd_;
	std::string buf_;
	static constexpr size_t maxLine = 64 * 1024;

public:
	explicit LineReader(int fd) : fd_(fd), buf_("") {}

	bool read_line(std::string& line){
		char tmp[4096];
		while (true)
		{
			size_t pos = buf_.find('\n');

			if (pos != std::string::npos)
			{
				line = buf_.substr(0, pos);
				buf_.erase(0, pos + 1);

				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				return true;
			}

			if (buf_.size() > maxLine)
				return false;

			ssize_t n = recv(fd_, tmp, sizeof(tmp), 0);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0)
				return false;

			buf_.append(tmp, n);
		}
	}
};

bool send_all(int fd, const std::string& data){
	size_t sent = 0;

	while (sent < data.size())
	{
		ssize_t n = send(fd, data.data() + sent, data.size() - sent, 0);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;

		sent += n;
	}

	return true;
}

void handle_client(SocketHandle client){
	LineReader reader(client.get());
	std::string line;

	while(reader.read_line(line))
	{
		if (!send_all(client.get(), line + "!\n"))
			break;
	}
}

int main(){
	signal(SIGPIPE, SIG_IGN);

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

			handle_client(SocketHandle(fd));
		}
	} catch(const std::exception& e)
	{
		std::cerr <<"fatal: " << e.what() << "\n";
		return 1;
	}
}