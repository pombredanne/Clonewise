#include "Clonewise.h"
#include <mpi.h>
#include <cstdio>
#include <cstdlib>
#include <omp.h>

#define TAG1		100

std::vector<std::string> vPackages;
std::list<int> packageQueue;
bool doEmbedded = false;
bool checkCacheOnly = true;
std::string cachePath;

struct Msg {
	int r[2];
	char *result;
};

std::list<Msg> messages;

void DoWork(int index);
static void MakeCache(const std::string &prefix, int argc, char *argv[], bool _doEmbedded);

void
DoWorkLoop(int me)
{
	MPI_Status status;
	int index;

	MPI_Send(&me, 1, MPI_INT, 0, TAG1, MPI_COMM_WORLD); 
	while (1) {
		MPI_Recv(&index, 1, MPI_INT, 0, TAG1, MPI_COMM_WORLD, &status); 
		if (index == -1)
			break;
		DoWork(index);
		MPI_Send(&me, 1, MPI_INT, 0, TAG1, MPI_COMM_WORLD); 
	}
	std::list<Msg>::iterator mIter;
	for (	mIter  = messages.begin();
		mIter != messages.end();
		mIter++)
	{
		MPI_Send(&me, 1, MPI_INT, 0, TAG1, MPI_COMM_WORLD); 
		MPI_Send(mIter->r, 1, MPI_INT, 0, TAG1, MPI_COMM_WORLD); 
		MPI_Send(mIter->result, mIter->r[1], MPI_CHAR, 0, TAG1, MPI_COMM_WORLD); 
		delete [] mIter->result;
	} 
}

void
DoWork(int index)
{
	const char *name = vPackages[index].c_str();
	int c = 1;
	char myName[strlen(name) + 1];
	char *v[] = { myName, NULL };
	char s[1024];

	strcpy(myName, name);

	snprintf(s, sizeof(s), "/var/lib/Clonewise/clones/distros/%s/%s/%s", distroString, cachePath.c_str(), name);
	if (0 && access(s, R_OK) == 0) {
		outFd = fopen(s, "r");
	} else {
		outFd = fopen(s, "w+");
		RunClonewise(c, v, doEmbedded);
	}
	printf("# scanned %s\n", name);
	fflush(stdout);

	Msg msg;
	long size;

	size = ftell(outFd);
	fseek(outFd, 0, SEEK_SET);
	msg.result = new char[size];
	fread(msg.result, 1, size, outFd);
	msg.r[0] = index;
	msg.r[1] = size;
	messages.push_back(msg);

	fclose(outFd), outFd = NULL;
}

bool embeddedOnly = true;

static void
Usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s [-d distroString] [-j numThreads] [-a]\n", argv0);
	exit(1);
}

void
MakeCache(int argc, char *argv[])
{
	MakeCache("cache", argc, argv, false);
}

void
MakeEmbeddedCache(int argc, char *argv[])
{
	MakeCache("cache-embedded", argc, argv, true);
}

static void
MakeCache(const std::string &prefix, int argc, char *argv[], bool _doEmbedded)
{
	MPI_Status status;
	int xi;
 	std::map<std::string, std::list<std::string> >::const_iterator pIter;
	int np, me; 

	cachePath = prefix;

	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &np);
	MPI_Comm_rank(MPI_COMM_WORLD, &me);

	doEmbedded = _doEmbedded;

	LoadEverything();
//	outputFormat = CLONEWISE_OUTPUT_XML;
	printf("# loaded everything\n");
	fflush(stdout);

	if (embeddedOnly) {
		std::map<std::string, std::set<std::string> >::const_iterator eIter;

		if (embeddedList.size() == 0) {
			char s[1024];

		        snprintf(s, sizeof(s), "/var/lib/Clonewise/clones/distros/%s/embedded-code-copies", distroString);
		        LoadEmbeddedCodeCopiesList(s);
		}
		for (	eIter  = embeddedList.begin(), xi = 0;
			eIter != embeddedList.end();
			eIter++)
		{
			vPackages.push_back(eIter->first);
			packageQueue.push_back(xi++);
		}
	} else {
		for (	pIter  = packages.begin(), xi = 0;
			pIter != packages.end();
			pIter++)
		{
			vPackages.push_back(pIter->first);
			packageQueue.push_back(xi++);
		}
	}

	printf("# going to scan %i packages\n", xi);
	fflush(stdout);
	if (me == 0) {
		while (packageQueue.size() != 0) {
			int index, which;

			MPI_Recv(&which, 1, MPI_INT, MPI_ANY_SOURCE, TAG1, MPI_COMM_WORLD, &status); 
			index = packageQueue.front();
			packageQueue.pop_front();
			MPI_Send(&index, 1, MPI_INT, which, TAG1, MPI_COMM_WORLD); 
		}
		for (int i = 1; i < np; i++) {
			int which, neg = -1;

			MPI_Recv(&which, 1, MPI_INT, i, TAG1, MPI_COMM_WORLD, &status); 
			MPI_Send(&neg, 1, MPI_INT, i, TAG1, MPI_COMM_WORLD); 
		}
		for (size_t i = 0; i < vPackages.size(); i++) { 
			int which;
			int r[2], size;
			char *result;
			FILE *f;
			char s[1024];

			MPI_Recv(&which, 1, MPI_INT, MPI_ANY_SOURCE, TAG1, MPI_COMM_WORLD, &status); 
			MPI_Recv(r, 2, MPI_INT, which, TAG1, MPI_COMM_WORLD, &status); 

			size = r[1];
			result = new char[size];

			MPI_Recv(result, size, MPI_CHAR, which, TAG1, MPI_COMM_WORLD, &status); 

			snprintf(s, sizeof(s), "/var/lib/Clonewise/clones/distros/%s/%s/%s", distroString, prefix.c_str(), vPackages[r[0]].c_str());
			f = fopen(s, "w");
			fwrite(result, 1, size, f);
			fclose(f), f = NULL;

			delete [] result;
		}
	} else {
		DoWorkLoop(me);
	}

	MPI_Finalize(); 
	exit(0); 
}

int
Clonewise_make_cache(int argc, char* argv[])
{ 
	int ch;

	ClonewiseInit();

	while ((ch = getopt(argc, argv, "ad:j:")) != EOF) {
		switch (ch) {
		case 'j':
			omp_set_num_threads(atoi(optarg));
			break;

		case 'a':
			embeddedOnly = false;
			break;

                case 'd':
			useDistroString = true;
                        distroString = optarg;
                        break;

		default:
			Usage(argv[0]);
			break;
		}
	}

	argc -= optind;
	argv += optind;
	
	MakeCache(argc, argv);
}
