#ifndef ASYNC_LOADER_H
#define ASYNC_LOADER_H

#include <list>
#include <string>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>


/// Asynchronous loader
///
/// It creates a thread that loads files on demand. It accepts requests (in the form of a filename of the file to load,
/// a pointer to a function for the way to load the file and a generic pointer for the data to load them to). Its not
/// meant to be destroyed because of a deadlock.
class AsyncLoader
{
	public:
		/// Default constructor starts the thread
		AsyncLoader() {start();}
		
		/// Do nothing
		~AsyncLoader() {}

		/// Tell me what to load, how to load it and where to store it. This puts a new loading request in the stack
		/// @param filename The file to load
		/// @param func How to load the file. The function should gets a filename (const char*) and the storage. It returns
		/// false if there was a loading error
		/// @param storage This points to the storage that the loader will store the data. The storage should not be
		/// destroyed from other threads
		void load(const char* filename, bool (*func)(const char*, void*), void* storage);

		/// Query the loader and see if its got something
		/// @param[out] filename The file that finished loading
		/// @param[out] storage The data are stored in this buffer
		/// @param[out] ok Its true if the loading of the resource was ok
		/// @return Return true if there is something that finished loading
		bool getLoaded(std::string& filename, void* storage, bool& ok);

	private:
		/// A loading request
		struct Request
		{
			std::string filename;
			bool (*func)(const char*, void*);
			void* storage;
		};

		/// It contains a few things to identify the response
		struct Response
		{
			std::string filename;
			void* storage;
			bool ok;
		};

		std::list<Request> requests;
		std::list<Response> responses;
		boost::mutex mutexReq; ///< Protect the requests container
		boost::mutex mutexResp; ///< Protect the responses container
		boost::thread thread;
		boost::condition_variable condVar;

		void workingFunc(); ///< The thread function. It waits for something in the requests container
		void start(); ///< Start thread
};


#endif