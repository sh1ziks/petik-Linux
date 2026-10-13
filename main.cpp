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
	#include <unordered_map>
	#include <mutex>
	#include <sstream>
	#include <optional>
	#include <cassert>
	#include <vector>
	#include <thread>
	#include <queue>
	#include <functional>
	#include <condition_variable>
	#include <memory>


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

	class  KVStore{
	private:
		std::unordered_map<std::string, std::string> data_;
		mutable std::mutex m_;

	public:
		void set(const std::string& key, std::string value)
		{
			std::lock_guard lock(m_);
			data_[key] = std::move(value);
		}

		std::optional<std::string> get(const std::string& key) const{
			std::lock_guard lock(m_);
			auto it = data_.find(key);

			if (it != data_.end())
				return it->second;

			return std::nullopt;
		}

		bool del(const std::string& key){
			std::lock_guard lock(m_);

			return data_.erase(key) > 0;
		}

		size_t size() const{
			std::lock_guard lock(m_);

			return data_.size();
		}
	};

	std::string handle_command(KVStore& store, const std::string& line){
		std::istringstream iss(line);
		std::string cmd;
		std::string key;

		if (!(iss >> cmd))
			return "ERR unknown command";

		if (cmd == "SET")
		{
			if (!(iss >> key))
				return "ERR wrong number of arguments";

			std::string value;
			std::getline(iss >> std::ws, value);
			if (value.empty())
				return "ERR wrong number of arguments";
			store.set(key, std::move(value));

			return "OK";
		}
		else if (cmd == "GET")
		{
			if (!(iss >> key))
				return "ERR wrong number of arguments";

			std::string extra;
			if (iss >> extra)
				return "ERR wrong number of arguments";

			auto tmp = store.get(key);
			if (!tmp)
				return "NOT_FOUND";

			return *tmp;
		}
		else if (cmd == "DEL")
		{
			if (!(iss >> key))
				return "ERR wrong number of arguments";

			std::string extra;
			if (iss >> extra)
				return "ERR wrong number of arguments";

			if (store.del(key))
				return "OK";
			else
				return "NOT_FOUND";
		}
		
		return "ERR unknown command";
	}

	void handle_client(SocketHandle client, KVStore& store){
		LineReader reader(client.get());
		std::string line;

		while(reader.read_line(line))
		{
			std::string reply = handle_command(store, line) + "\n";
			if (!send_all(client.get(), reply))
				break;
		}
	}


	class ThreadPool{
	private:
		std::vector<std::thread> workers_;
		std::queue<std::function<void()>> tasks_;
		std::mutex m_;
		std::condition_variable cv_;
		bool stop_ = false;

	public:
		explicit ThreadPool(size_t n){
			for (size_t i = 0; i < n; i++)
				workers_.emplace_back([this]{
					while(true)
					{
						std::function<void()> task;
						{
							std::unique_lock<std::mutex> lock(m_);
							cv_.wait(lock, [this] {return stop_ || !tasks_.empty();});

							if (stop_ && tasks_.empty())
								return;
							task = std::move(tasks_.front());
							tasks_.pop();
						}
						try
						{
							task();
						} catch (const std::exception& e)
						{
							std::cerr << "task failed: " << e.what() << "\n";
						} catch (...)
						{
							std::cerr << "task failed: unknown error\n";
						}
					}
				});
		}

		void enqueue (std::function<void()> f){
			{
				std::lock_guard<std::mutex> lock(m_);
				tasks_.push(std::move(f));
			}
			cv_.notify_one();
		}

		~ThreadPool(){
			{
				std::lock_guard<std::mutex> lock(m_);
				stop_ = true;
			}
			cv_.notify_all();
			for (auto& w : workers_)
				w.join();
		}
	};

	int main(){
		signal(SIGPIPE, SIG_IGN);
		KVStore store;
		ThreadPool pool(4);

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

				SocketHandle tmp(fd);
				std::shared_ptr<SocketHandle> sock = std::make_shared<SocketHandle>(std::move(tmp));
				pool.enqueue([sock, &store] {handle_client(std::move(*sock), store);});
			}
		} catch(const std::exception& e)
		{
			std::cerr <<"fatal: " << e.what() << "\n";
			return 1;
		}
	}