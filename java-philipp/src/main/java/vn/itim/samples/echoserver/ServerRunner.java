package vn.itim.samples.echoserver;

import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;

public class ServerRunner {

	public static void main(String []args) throws UnknownHostException, IOException {
		Server s = new Server(InetAddress.getByName("0.0.0.0"), 50112);
		s.start();
	}
}
