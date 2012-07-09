package vn.itim.samples.echoserver;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.concurrent.atomic.AtomicLong;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Server {

	private static final Logger LOG = LoggerFactory.getLogger(Server.class);

	private final int port;
	private final InetAddress hostAddress;
	private ServerSocketChannel serverChannel;

	private IOScheduler[] readerWriters;
	private AtomicLong readerWriterSequence = new AtomicLong(0);

	public Server(InetAddress addr, int port) throws IOException {
		this.port = port;
		this.hostAddress = addr;

		Runtime runtime = Runtime.getRuntime();
		readerWriters = new IOScheduler[runtime.availableProcessors()+1];
//		readerWriters[0] =  new IOScheduler(new NewConnectionProcessor());

		ReadWriteProcessor ioProcessor = new ReadWriteProcessor();
		for (int i=1; i < readerWriters.length; i++) {
			readerWriters[i] = new IOScheduler(ioProcessor);
		}
	}

	public void start() throws IOException {
		for (int i=readerWriters.length-1; i >=0 ; i--) {
			readerWriters[i].start();
		}

	    this.serverChannel = ServerSocketChannel.open();
	    serverChannel.configureBlocking(false);

	    InetSocketAddress isa = new InetSocketAddress(this.hostAddress, this.port);
	    serverChannel.socket().setReuseAddress(true);
	    serverChannel.socket().bind(isa, 4096);

	    readerWriters[0].register(serverChannel, SelectionKey.OP_ACCEPT);
	}

	public void stop() {
		for (int i=0; i < readerWriters.length ; i++) {
			readerWriters[i].stop();
		}
	}

	private final class NewConnectionProcessor implements SelectorProcessor {

		@Override
		public boolean process(SelectionKey key) throws IOException {
			if (key.isAcceptable()) {
				// For an accept to be pending the channel must be a server socket channel.
			    ServerSocketChannel serverSocketChannel = (ServerSocketChannel) key.channel();

			    int num = 1+(int)(readerWriterSequence.getAndIncrement() % (readerWriters.length-1));
			    readerWriters[num].register(serverSocketChannel.accept(), SelectionKey.OP_READ);
			}

			return false;
		}

		@Override
		public void handleBeforeClose(SelectionKey key) {}

	}

	private final class ReadWriteProcessor implements SelectorProcessor {

		@Override
		public void handleBeforeClose(SelectionKey key) {
			key.attach(null);
		}

		@Override
		public boolean process(SelectionKey key) throws IOException {
	    	SocketChannel socketChannel = (SocketChannel) key.channel();
			ByteBuffer buffer = (ByteBuffer) key.attachment();
			if (buffer == null) {
				buffer = ByteBuffer.allocateDirect(8192);
				key.attach(buffer);
			}

			if (key.isReadable()) {
				buffer.clear();
				int numRead = socketChannel.read(buffer);

				if (numRead > -1) {
					buffer.flip();
					key.interestOps(SelectionKey.OP_WRITE);
				}

				return !(numRead > -1);
			} else if (key.isWritable()) {
				socketChannel.write(buffer);
				if (!buffer.hasRemaining()) {
					key.interestOps(SelectionKey.OP_READ);
				}
			}
			return false;
		}
	}
}
