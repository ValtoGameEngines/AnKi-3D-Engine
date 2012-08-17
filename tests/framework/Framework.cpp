#include "tests/framework/Framework.h"
#include <iostream>
#include <cstring>

namespace anki {

//==============================================================================
void Test::run()
{
	std::cout << "========\nRunning " << suite->name << " " << name
		<< "\n========" << std::endl;
	callback(*this);
}

//==============================================================================
void Tester::addTest(const char* name, const char* suiteName,
	TestCallback callback)
{
	PtrVector<TestSuite>::iterator it;
	for(it = suites.begin(); it != suites.end(); it++)
	{
		if((*it)->name == suiteName)
		{
			break;
		}
	}

	// Not found
	TestSuite* suite = nullptr;
	if(it == suites.end())
	{
		suite = new TestSuite;
		suite->name = suiteName;
		suites.push_back(suite);
	}
	else
	{
		suite = *it;
	}

	// Sanity check
	PtrVector<Test>::iterator it1;
	for(it1 = suite->tests.begin(); it1 != suite->tests.end(); it1++)
	{
		if((*it)->name == name)
		{
			std::cerr << "Test already exists: " << name << std::endl;
			return;
		}
	}

	// Add the test
	Test* test = new Test;
	suite->tests.push_back(test);
	test->name = name;
	test->suite = suite;
	test->callback = callback;
}

//==============================================================================
int Tester::run(int argc, char** argv)
{
	// Parse args
	//
	programName = argv[0];

	std::string helpMessage = "Usage: " + programName + R"( [options]
Options:
  --help         Print this message
  --list-tests   List all the tests
  --suite <name> Run tests only from this suite
  --test <name>  Run this test. --suite needs to be specified)";

	std::string suiteName;
	std::string testName;

	for(int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		if(strcmp(arg, "--list-tests") == 0)
		{
			listTests();
			return 0;
		}
		else if(strcmp(arg, "--help") == 0)
		{
			std::cout << helpMessage << std::endl;
			return 0;
		}
		else if(strcmp(arg, "--suite") == 0)
		{
			++i;
			if(i >= argc)
			{
				std::cerr << "<name> is missing after --suite" << std::endl;
				return 1;
			}
			suiteName = argv[i];
		}
		else if(strcmp(arg, "--test") == 0)
		{
			++i;
			if(i >= argc)
			{
				std::cerr << "<name> is missing after --test" << std::endl;
				return 1;
			}
			testName = argv[i];
		}
	}

	// Sanity check
	if(testName.length() > 0 && suiteName.length() == 0)
	{
		std::cout << "Specify --suite as well" << std::endl;
		return 1;
	}

	// Run tests
	//
	int passed = 0;
	int run = 0;
	if(argc == 1)
	{
		// Run all
		for(TestSuite* suite : suites)
		{
			for(Test* test : suite->tests)
			{
				++run;
				try
				{
					test->run();
					++passed;
				}
				catch(const std::exception& e)
				{
					std::cerr << e.what() << std::endl;
				}
			}
		}
	}
	else
	{
		for(TestSuite* suite : suites)
		{
			if(suite->name == suiteName)
			{
				for(Test* test : suite->tests)
				{
					if(test->name == testName || testName.length() == 0)
					{
						++run;
						try
						{
							test->run();
							++passed;
						}
						catch(const std::exception& e)
						{
							std::cerr << e.what() << std::endl;
						}
					}
				}
			}
		}
	}

	std::cout << "========\nRun " << run << " tests, passed " << passed
		<< "\n" << std::endl;

	return run - passed;
}

//==============================================================================
int Tester::listTests()
{
	for(TestSuite* suite : suites)
	{
		for(Test* test : suite->tests)
		{
			std::cout << programName << " --suite \"" << suite->name
				<< "\" --test \"" << test->name << "\"" << std::endl;
		}
	}

	return 0;
}

} // end namespace anki