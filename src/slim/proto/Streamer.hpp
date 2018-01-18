/*
 * Copyright 2017, Andrej Kislovskij
 *
 * This is PUBLIC DOMAIN software so use at your own risk as it comes
 * with no warranties. This code is yours to share, use and modify without
 * any restrictions or obligations.
 *
 * For more information see conwrap/LICENSE or refer refer to http://unlicense.org
 *
 * Author: gimesketvirtadieni at gmail dot com (Andrej Kislovskij)
 */

#pragma once

#include <algorithm>
#include <chrono>
#include <conwrap/ProcessorProxy.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>  // std::stringstream
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "slim/log/log.hpp"
#include "slim/proto/CommandSession.hpp"
#include "slim/proto/StreamingSession.hpp"


namespace slim
{
	namespace proto
	{
		template<typename ConnectionType>
		class Streamer
		{
			template<typename SessionType>
			using SessionsMap = std::unordered_map<ConnectionType*, std::unique_ptr<SessionType>>;

			public:
				Streamer()
				: timerThread{[&]
				{
					LOG(DEBUG) << "Timer thread started";

					for(unsigned int counter{0}; timerRunning; counter++, std::this_thread::sleep_for(std::chrono::milliseconds{200}))
			        {
						if (counter > 24)
						{
							// TODO: use std::optional
							if (processorProxyPtr)
							{
								processorProxyPtr->process([&]
								{
									// sending ping command to measure round-trip latency
									for (auto& sessionEntry : commandSessions)
									{
										sessionEntry.second->ping();
									}
								});
							}

							counter = 0;
						}
			        }

					LOG(DEBUG) << "Timer thread stopped";
				}} {}

			   ~Streamer()
				{
					timerRunning = false;
					timerThread.join();
				}

				Streamer(const Streamer&) = delete;             // non-copyable
				Streamer& operator=(const Streamer&) = delete;  // non-assignable
				Streamer(Streamer&& rhs) = delete;              // non-movable
				Streamer& operator=(Streamer&& rhs) = delete;   // non-movable-assinable

				inline bool onChunk(Chunk& chunk, unsigned int sr)
				{
					auto done{true};

					if (sr && samplingRate != sr)
					{
						// resetting current sampling rate to zero so futher routine can handle it
						samplingRate = 0;

						// stopping all streaming sessions which will make them reconnect using a new sampling rate
						for (auto& sessionEntry : streamingSessions)
						{
							sessionEntry.first->stop();
						}
					}

					if (sr && !samplingRate)
					{
						// deferring chunk transmition and setting a new sampling rate
						done         = false;
						samplingRate = sr;

						// sending command to start streaming
						for (auto& sessionEntry : commandSessions)
						{
							// TODO: use MAC provided in HELO message
							std::stringstream ss;
					        ss << static_cast<const void*>(sessionEntry.second.get());

					        sessionEntry.second->send(CommandSTRM{CommandSelection::Start, samplingRate, ss.str()});
						}
					}

					if (sr && samplingRate == sr && done)
					{
						// TODO: these validation should be optimized by using internal status
						auto finish{hasToFinish()};

						// if there is time for streaming validations
						if (!finish)
						{
							// if amount of HTTP session is not equal to SlimProto sessions
							if (streamingSessions.size() != commandSessions.size())
							{
								LOG(DEBUG) << "Deferring chunk transmition due to missing HTTP sessions";
								done = false;

								// TODO: implement cruise control; for now sleep is good enough
								// this sleep prevents from busy spinning until all HTTP sessions reconnect
								std::this_thread::sleep_for(std::chrono::milliseconds{20});
							}
							else
							{
								// making sure all HTTP sessions have reconnected so they can use a new sampling rate
								for (auto& sessionEntry : streamingSessions)
								{
									if (samplingRate != sessionEntry.second->getSamplingRate())
									{
										LOG(DEBUG) << "Deferring chunk transmition due to HTTP sessions reconnect";
										done = false;

										// TODO: implement cruise control; for now sleep is good enough
										// this sleep prevents from busy spinning until all HTTP sessions reconnect
										std::this_thread::sleep_for(std::chrono::milliseconds{20});
										break;
									}
								}
							}
						}
						else
						{
							LOG(DEBUG) << "Could not defer chunk processing due to reached threashold";
						}

						// if there is no need to defer chunk processing
						if (done)
						{
							// TODO: this approach is not good enough; HTTP sessions should be linked with SlimProto session
							auto totalClients{commandSessions.size()};
							auto counter{totalClients};

							// resetting period during which chunk processing can be deferred
							deferStarted.reset();

							for (auto& sessionEntry : streamingSessions)
							{
								if (samplingRate == sessionEntry.second->getSamplingRate())
								{
									sessionEntry.second->onChunk(chunk, samplingRate);
									counter--;
								}
							}

							if (counter)
							{
								LOG(WARNING) << "Current chunk transmition was skipped for " << counter << " client(s)";
							}
						}
					}

					return done;
				}

				void onHTTPClose(ConnectionType& connection)
				{
					LOG(INFO) << "HTTP close callback";

					removeSession(streamingSessions, connection);
				}

				void onHTTPData(ConnectionType& connection, unsigned char* buffer, std::size_t receivedSize)
				{
					LOG(INFO) << "HTTP data callback receivedSize=" << receivedSize;

					if (!applyToSession(streamingSessions, connection, [&](StreamingSession<ConnectionType>& session)
					{
						session.onRequest(buffer, receivedSize);
					}))
					{
						LOG(INFO) << "HTTP request received";

						// TODO: refactor to a different class
						std::string get{"GET"};
						std::string s{(char*)buffer, get.size()};
						if (!get.compare(s))
						{
							// TODO: use std::optional
							auto  clientID{StreamingSession<ConnectionType>::parseClientID({(char*)buffer, receivedSize})};
							auto* commandSessionPtr{(CommandSession<ConnectionType>*)nullptr};
							if (clientID.length() > 0)
							{
								LOG(INFO) << "Client ID was parsed from HTTP request (clientID=" << clientID << ")";

								// TODO: use hashmap id
								std::find_if(commandSessions.begin(), commandSessions.end(), [&](auto& sessionEntry)
								{
									auto found{false};

									if (sessionEntry.second->getClientID() == clientID)
									{
										found             = true;
										commandSessionPtr = sessionEntry.second.get();
									}

									return found;
								});
							}

							// if there a SlimProto connection found that originated this HTTP request
							if (commandSessionPtr)
							{
								// TODO: work in progress
								auto sessionPtr{std::make_unique<StreamingSession<ConnectionType>>(connection, clientID, 2, samplingRate, 32)};
								sessionPtr->onRequest(buffer, receivedSize);

								// saving streaming session
								addSession(streamingSessions, connection, std::move(sessionPtr));
							}
							else
							{
								// closing HTTP connection due to incorrect reference to SlimProto session
								LOG(ERROR) << "Could not correlate HTTP request with a valid SlimProto session";
								connection.stop();
							}
						}
					}
				}

				void onHTTPOpen(ConnectionType& connection)
				{
					LOG(INFO) << "HTTP open callback";
				}

				void onHTTPStart(ConnectionType& connection)
				{
					LOG(INFO) << "HTTP start callback";
				}

				void onHTTPStop(ConnectionType& connection)
				{
					LOG(INFO) << "HTTP stop callback";
				}

				void onSlimProtoClose(ConnectionType& connection)
				{
					LOG(INFO) << "SlimProto close callback";

					removeSession(commandSessions, connection);
				}

				void onSlimProtoData(ConnectionType& connection, unsigned char* buffer, std::size_t receivedSize)
				{
					LOG(INFO) << "SlimProto data callback receivedSize=" << receivedSize;

					if (!applyToSession(commandSessions, connection, [&](CommandSession<ConnectionType>& session)
					{
						session.onRequest(buffer, receivedSize);
					}))
					{
						// TODO: refactor to a different class
						std::string helo{"HELO"};
						std::string s{(char*)buffer, helo.size()};
						if (!helo.compare(s))
						{
							LOG(INFO) << "HELO command received";

							auto sessionPtr{std::make_unique<CommandSession<ConnectionType>>(connection)};
							sessionPtr->onRequest(buffer, receivedSize);
							addSession(commandSessions, connection, std::move(sessionPtr));
						}
						else
						{
							LOG(INFO) << "Incorrect handshake message received";
							connection.stop();
						}
					}
				}

				void onSlimProtoOpen(ConnectionType& connection)
				{
					LOG(INFO) << "SlimProto open callback";
				}

				void onSlimProtoStart(ConnectionType& connection)
				{
					LOG(INFO) << "SlimProto start callback";
				}

				void onSlimProtoStop(ConnectionType& connection)
				{
					LOG(INFO) << "SlimProto stop callback";
				}

				void setProcessorProxy(conwrap::ProcessorProxy<ContainerBase>* p)
				{
					processorProxyPtr = p;
				}

			protected:
				template<typename SessionType>
				inline auto& addSession(SessionsMap<SessionType>& sessions, ConnectionType& connection, std::unique_ptr<SessionType> sessionPtr)
				{
					LOG(DEBUG) << LABELS{"slim"} << "Adding new session (sessions=" << sessions.size() << ")...";

					auto* s{(SessionType*)nullptr};
					auto  found{sessions.find(&connection)};

					if (found == sessions.end())
					{
						s = sessionPtr.get();

						// saving session in a map; using pointer to a relevant connection as an ID
						sessions[&connection] = std::move(sessionPtr);
						LOG(DEBUG) << LABELS{"slim"} << "New session was added (id=" << s << ", sessions=" << sessions.size() << ")";
					}
					else
					{
						s = (*found).second.get();
						LOG(INFO) << "Session already exists";
					}

					return *s;
				}

				template<typename SessionType, typename FunctionType>
				inline bool applyToSession(SessionsMap<SessionType>& sessions, ConnectionType& connection, FunctionType fun)
				{
					auto found{sessions.find(&connection)};

					if (found != sessions.end())
					{
						fun(*(*found).second);
					}

					return (found != sessions.end());
				}

				inline bool hasToFinish()
				{
					auto finish{false};

					if (deferStarted.has_value())
					{
						auto diff{std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - deferStarted.value()).count()};
						finish = (diff > 100);
					}
					else
					{
						deferStarted = std::chrono::steady_clock::now();
					}

					return finish;
				}

				template<typename SessionType>
				inline void removeSession(SessionsMap<SessionType>& sessions, ConnectionType& connection)
				{
					LOG(DEBUG) << LABELS{"slim"} << "Removing session (sessions=" << sessions.size() << ")...";

					auto found{sessions.find(&connection)};

					if (found != sessions.end())
					{
						auto* s{(*found).second.get()};
						sessions.erase(found);
						LOG(DEBUG) << LABELS{"slim"} << "Session was removed (id=" << s << ", sessions=" << sessions.size() << ")";
					}
				}

			private:
				SessionsMap<CommandSession<ConnectionType>>   commandSessions;
				SessionsMap<StreamingSession<ConnectionType>> streamingSessions;
				unsigned int                                  samplingRate{0};
				conwrap::ProcessorProxy<ContainerBase>*       processorProxyPtr{nullptr};
				volatile bool                                 timerRunning{true};
				std::thread                                   timerThread;

				using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
				std::optional<TimePoint> deferStarted{std::nullopt};
		};
	}
}
