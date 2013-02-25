#include <Calaos.h>
#include <iostream>

using namespace std;

int main (int argc, char **argv)
{
	if (argc < 2) return 1;
	
        string url, url_encoded;
	
	url = argv[1];
	url_encoded = Calaos::url_decode2(url);
	
	cout << url << endl << url_encoded << endl;	

	return 0;
}
