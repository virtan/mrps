package vn.itim.samples.echoserver;

import java.io.IOException;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.channels.spi.SelectorProvider;
import java.util.Iterator;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IOScheduler {

	private static final AtomicInteger sequence = new AtomicInteger(0);
	private static final Logger LOG = LoggerFactory.getLogger(IOScheduler.class);

	private Selector selector;
	private SelectorProcessor processor;

	private volatile boolean isRunning;
	private Thread runner = new Thread(new Worker(), "IOScheduler-"+sequence.getAndIncrement());

	private ConcurrentLinkedQueue<RegisterRequests> requests = new ConcurrentLinkedQueue<RegisterRequests>();

	public IOScheduler(SelectorProcessor proc) throws IOException {
		this.selector = SelectorProvider.provider().openSelector();
		this.processor = proc;
	}

	public void start() {
		isRunning = true;
		runner.start();
	}

	public void stop() {
		isRunning = false;
		try {
			runner.join();
		} catch (InterruptedException e) {
			e.printStackTrace();
		}
	}

	public void register(SocketChannel socketChannel, int interest) {
		requests.offer(new RegisterRequests(interest, socketChannel));
		selector.wakeup();
	}

	public void register(ServerSocketChannel serverChannel, int interest) {
		requests.offer(new RegisterRequests(interest, serverChannel));
		selector.wakeup();
	}

	private class Worker implements Runnable {
		@Override
		public void run() {
			Thread currentThread = Thread.currentThread();
			while (isRunning && !currentThread.isInterrupted()) {
				try {
					int available = 0;

					int counter = 50;
					while (--counter > 0 && (available = selector.selectNow()) == 0) {
						Thread.yield();
					}

					if (available == 0) {
						selector.select();
					}
				} catch (IOException e) {
					e.printStackTrace();
				}

				Iterator<SelectionKey> selectedKeys = selector.selectedKeys().iterator();
				while (selectedKeys.hasNext()) {
					SelectionKey key = selectedKeys.next();
					selectedKeys.remove();

					boolean keyShouldBeClosed = true;
					try {
						if (key.isValid()) {
							keyShouldBeClosed = processor.process(key);
						}
					} catch (IOException e) {
						LOG.error("Failed to complete key operation: {}", e.getMessage());
					}

					if (keyShouldBeClosed) {
						try {
							processor.handleBeforeClose(key);

							SocketChannel socketChannel = (SocketChannel) key.channel();
							socketChannel.close();
						} catch (IOException e) {
							LOG.info("Remote host has closed connection: {}", key);
						}
					}
				}

				while (!requests.isEmpty()) {
					RegisterRequests req = requests.poll();
					try {
						if (req.cChannel != null) {
							LOG.info("New icoming connection: {}", req.cChannel.socket().getRemoteSocketAddress());
						    req.cChannel.configureBlocking(false);
						    req.cChannel.socket().setKeepAlive(true);
						    req.cChannel.socket().setKeepAlive(true);
						    req.cChannel.socket().setTcpNoDelay(true);
						    req.cChannel.socket().setReceiveBufferSize(8192);
						    req.cChannel.socket().setSendBufferSize(8192);
							req.cChannel.register(selector, req.interest);
						} else {
							req.sChannel.register(selector, req.interest);
						}
					} catch (Exception e) {
						LOG.error("Failed to register key interest: {}", req);
						try {
							if (req.cChannel != null) {
								req.cChannel.close();
							} else {
								req.sChannel.close();
							}
						} catch (IOException e1) {
							LOG.error("Failed to close channel: {}", req);
						}
					}
				}
			}
		}
	}

	private final static class RegisterRequests {
		public final int interest;
		public final SocketChannel cChannel;
		public final ServerSocketChannel sChannel;

		public RegisterRequests(int interest, SocketChannel channel) {
			this.interest = interest;
			this.cChannel = channel;
			this.sChannel = null;
		}

		public RegisterRequests(int interest, ServerSocketChannel channel) {
			this.interest = interest;
			this.sChannel = channel;
			this.cChannel = null;
		}

		@Override
		public String toString() {
			return "RegisterRequests [interest=" + interest + ", cChannel=" + cChannel + ", sChannel=" + sChannel + "]";
		}
	}
}
