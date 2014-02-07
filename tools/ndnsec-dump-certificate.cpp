/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2013, Regents of the University of California
 *                     Yingdi Yu
 *
 * BSD license, See the LICENSE file for more information
 *
 * Author: Yingdi Yu <yingdi@cs.ucla.edu>
 */

#include <iostream>
#include <fstream>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio.hpp>

#include <cryptopp/base64.h>
#include <cryptopp/files.h>

#include "security/key-chain.hpp"

using namespace ndn;
namespace po = boost::program_options;

shared_ptr<IdentityCertificate>
getIdentityCertificate(const std::string& fileName)
{
  std::istream* ifs;
  if(fileName == "-")
    ifs = &std::cin;
  else
    ifs = new std::ifstream(fileName.c_str());

  std::string decoded;
  CryptoPP::FileSource ss2(*ifs, true,
                           new CryptoPP::Base64Decoder(new CryptoPP::StringSink(decoded)));

  ptr_lib::shared_ptr<IdentityCertificate> identityCertificate = ptr_lib::make_shared<IdentityCertificate>();
  identityCertificate->wireDecode(Block(decoded.c_str(), decoded.size()));

  return identityCertificate;
}

int main(int argc, char** argv)	
{
  std::string name;
  bool isKeyName = false;
  bool isIdentityName = false;
  bool isCertName = true;
  bool isFileName = false;
  bool isPretty = false;
  bool isStdOut = true;
  bool isRepoOut = false;
  std::string repoHost = "127.0.0.1";
  std::string repoPort = "7376";
  bool isDnsOut = false;

  po::options_description desc("General Usage\n  ndn-dump-certificate [-h] [-p] [-d] [-r [-H repo-host] [-P repor-port] ] [-i|k|f] certName\nGeneral options");
  desc.add_options()
    ("help,h", "produce help message")
    ("pretty,p", "optional, if specified, display certificate in human readable format")
    ("identity,i", "optional, if specified, name is identity name (e.g. /ndn/edu/ucla/alice), otherwise certificate name")
    ("key,k", "optional, if specified, name is key name (e.g. /ndn/edu/ucla/alice/KSK-123456789), otherwise certificate name")
    ("file,f", "optional, if specified, name is file name, - for stdin")
    ("repo-output,r", "optional, if specified, certificate is dumped (published) to repo")
    ("repo-host,H", po::value<std::string>(&repoHost)->default_value("localhost"), "optional, the repo host if repo-output is specified")
    ("repo-port,P", po::value<std::string>(&repoPort)->default_value("7376"), "optional, the repo port if repo-output is specified")
    ("dns-output,d", "optional, if specified, certificate is dumped (published) to DNS")
    ("name,n", po::value<std::string>(&name), "certificate name, for example, /ndn/edu/ucla/KEY/cs/alice/ksk-1234567890/ID-CERT/%FD%FF%FF%FF%FF%FF%FF%FF")
    ;

  po::positional_options_description p;
  p.add("name", 1);
  
  po::variables_map vm;
  po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
  po::notify(vm);

  if (vm.count("help")) 
    {
      std::cerr << desc << std::endl;
      return 1;
    }

  if (0 == vm.count("name"))
    {
      std::cerr << "identity_name must be specified" << std::endl;
      std::cerr << desc << std::endl;
      return 1;
    }
  
  if (vm.count("key"))
    {
      isCertName = false;
      isKeyName = true;
    }
  else if (vm.count("identity"))
    {
      isCertName = false;
      isIdentityName = true;
    }
  else if (vm.count("file"))
    {
      isCertName = false;
      isFileName = true;
    }    
    
  if (vm.count("pretty"))
    isPretty = true;

  if (vm.count("repo-output"))
    {
      isRepoOut = true;
      isStdOut = false;
    }
  else if(vm.count("dns-output"))
    {
      isDnsOut = true;
      isStdOut = false;
      std::cerr << "Error: DNS output is not supported yet!" << std::endl;
      return 1;
    }

  if (isPretty && !isStdOut)
    {
      std::cerr << "Error: pretty option can only be specified when other output option is specified" << std::endl;
      return 1;
    }

  KeyChain keyChain;
  ptr_lib::shared_ptr<IdentityCertificate> certificate;

  try{
    if(isIdentityName || isKeyName || isCertName)
      {
        if(isIdentityName)
          {
            Name certName = keyChain.getDefaultCertificateNameForIdentity(name);
            certificate = keyChain.getCertificate(certName);
          }
        else if(isKeyName)
          {
            Name certName = keyChain.getDefaultCertificateNameForKey(name);
            certificate = keyChain.getCertificate(certName);
          }
        else
          certificate = keyChain.getCertificate(name);
 
        if(NULL == certificate)
          {
            std::cerr << "No certificate found!" << std::endl;
            return 1;
          }
      }
    else
      {
        certificate = getIdentityCertificate(name);
        if(NULL == certificate)
          {
            std::cerr << "No certificate read!" << std::endl;
            return 1;
          }
      }

    if(isPretty)
      {
        std::cout << *certificate << std::endl;
        // cout << "Certificate name: " << std::endl;
        // cout << "  " << certificate->getName() << std::endl;
        // cout << "Validity: " << std::endl;
        // cout << "  NotBefore: " << boost::posix_time::to_simple_string(certificate->getNotBefore()) << std::endl;
        // cout << "  NotAfter: " << boost::posix_time::to_simple_string(certificate->getNotAfter()) << std::endl;
        // cout << "Subject Description: " << std::endl;
        // const vector<CertificateSubjectDescription>& SubDescriptionList = certificate->getSubjectDescriptionList();
        // vector<CertificateSubjectDescription>::const_iterator it = SubDescriptionList.begin();
        // for(; it != SubDescriptionList.end(); it++)
        //   cout << "  " << it->getOidStr() << ": " << it->getValue() << std::endl;
        // cout << "Public key bits: " << std::endl;
        // const Blob& keyBlob = certificate->getPublicKeygetKeyBlob();
        // std::string encoded;
        // CryptoPP::StringSource ss(reinterpret_cast<const unsigned char *>(keyBlob.buf()), keyBlob.size(), true,
        //                           new CryptoPP::Base64Encoder(new CryptoPP::StringSink(encoded), true, 64));
        // cout << encoded;        
      }
    else
      {
        if(isStdOut)
          {
            CryptoPP::StringSource ss(certificate->wireEncode().wire(), certificate->wireEncode().size(),
                                      true,
                                      new CryptoPP::Base64Encoder(new CryptoPP::FileSink(std::cout), true, 64));
            return 0;
          }
        if(isRepoOut)
          {
            using namespace boost::asio::ip;
            tcp::iostream request_stream;
#if (BOOST_VERSION >= 104700)
            request_stream.expires_from_now(boost::posix_time::milliseconds(3000));
#endif
            request_stream.connect(repoHost,repoPort);
            if(!request_stream)
              {
                std::cerr << "fail to open the stream!" << std::endl;
                return 1;
              }
            request_stream.write(reinterpret_cast<const char*>(certificate->wireEncode().wire()), certificate->wireEncode().size());
            return 0;
          }
      }
  }
  catch(std::exception & e){
    std::cerr << e.what() << std::endl;
    return 1;
  }
  return 0;
}