-- Socket operations that finish at once, on many sockets, while the GC reuses freed memory.
-- Under WinIO without FILE_SKIP_COMPLETION_PORT_ON_SUCCESS each one leaves a completion packet
-- pointing at a freed request record, and the process eventually crashes processing one.

import Control.Concurrent
import Control.Exception
import Control.Monad
import qualified Data.ByteString as BS
import qualified Data.ByteString.Char8 as B8
import Data.IORef
import Network.Socket
import Network.Socket.ByteString (recv, sendAll)
import System.Exit
import System.IO
import System.Mem (performMinorGC)

pairs, seconds :: Int
pairs = 64
seconds = 120

connectedPair :: IO (Socket, Socket)
connectedPair = do
  listener <- socket AF_INET Stream defaultProtocol
  bind listener (SockAddrInet 0 (tupleToHostAddress (127, 0, 0, 1)))
  listen listener 1
  port <- socketPort listener
  client <- socket AF_INET Stream defaultProtocol
  connect client (SockAddrInet port (tupleToHostAddress (127, 0, 0, 1)))
  (server, _) <- accept listener
  close listener
  return (client, server)

main :: IO ()
main = do
  hSetBuffering stdout LineBuffering
  received <- newIORef (0 :: Int)
  forM_ [1 .. pairs] $ \_ -> do
    (client, server) <- connectedPair
    _ <- forkIO $ forever $ sendAll server (B8.replicate 4096 'x')
    forkIO $ forever $ do
      bs <- recv client 16
      when (BS.null bs) $ throwIO (userError "unexpected EOF")
      atomicModifyIORef' received (\n -> (n + BS.length bs, ()))
      performMinorGC
  forM_ [1 .. 4 :: Int] $ \t -> forkIO $ forM_ (cycle [100 .. 3000]) $ \n -> do
    bs <- evaluate (BS.replicate (n + t) 1)
    evaluate (BS.index bs (n `div` 2))
  forM_ [1 .. seconds] $ \s -> do
    threadDelay 1000000
    when (s `mod` 10 == 0) $ readIORef received >>= \n -> putStrLn ("  " ++ show s ++ "s: " ++ show n ++ " bytes received")
  putStrLn ("survived " ++ show seconds ++ "s")
  exitSuccess
