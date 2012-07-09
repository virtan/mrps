package vn.itim.samples.echoserver;

import java.io.IOException;
import java.nio.channels.SelectionKey;

public interface SelectorProcessor {

	public boolean process (SelectionKey key) throws IOException;

	public void handleBeforeClose(SelectionKey key);
}
