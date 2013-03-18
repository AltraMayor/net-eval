/* Packet writer. */

#include <stdio.h>

int main(void)
{
	/* TODO Read parameters:
	 *	Prefix file name
	 *	Stack ("ip" or "xia")
	 *	Network interface (e.g. "eth0")
	 *	Ethernet address of router (e.g. "11:22:33:44:55:66")
	 *	Destination address template ("ip", "fb0", ..., "fb3", "via")
	 *	Packet size
	 *	Number of nodes (Packet writers plus router)
	 *	ID of this packet writer (1..(N-1))
	 *	Run (1..)
	 */

	/* TODO Load seeds. */

	/* TODO Load and shuffle destinations. */

	/* TODO Convert destinations to a binary vector. */

	/* TODO Cache Zipf sampling. */

	/* TODO Packet template. */
	/* TODO Loop: */
		/* TODO Sample destination and update packet template. */
		/* TODO Send packet out. */

	return 0;
}
