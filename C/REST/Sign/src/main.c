#include <stdio.h> /* printf, sprintf */
#include <stdlib.h> /* exit, atoi, malloc, free */
#include <string.h>
#include <curl/curl.h>
#include <inttypes.h> //int64_t, uint64_t
#include <time.h>
#include <json-c/json.h>

#define STRING_LENGTH 256
#define BODY_SIZE 1024

//#define DEBUG
#define CLOUD

struct response{
  char *ptr;
  size_t len;
};

void init_string(struct response *s) {
  s->len = 0;
  s->ptr = malloc(s->len+1);
  if (s->ptr == NULL) {
    fprintf(stderr, "malloc() failed\n");
    exit(EXIT_FAILURE);
  }
  s->ptr[0] = '\0';
}

char *ltrim(char *str, const char *seps) {
    size_t totrim;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    totrim = strspn(str, seps);
    if (totrim > 0) {
        size_t len = strlen(str);
        if (totrim == len) {
            str[0] = '\0';
        }
        else {
            memmove(str, str + totrim, len + 1 - totrim);
        }
    }
    return str;
}

char *rtrim(char *str, const char *seps) {
    int i;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    i = strlen(str) - 1;
    while (i >= 0 && strchr(seps, str[i]) != NULL) {
        str[i] = '\0';
        i--;
    }
    return str;
}

char *trim(char *str, const char *seps) {
    return ltrim(rtrim(str, seps), seps);
}

// https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
size_t write_callback(char *ptr, size_t size, size_t nmemb, struct response *s) {
    size_t new_len = s->len + size*nmemb;
    s->ptr = realloc(s->ptr, new_len+1);
    if (s->ptr == NULL) {
        fprintf(stderr, "realloc() failed\n");
        exit(EXIT_FAILURE);
    }
    memcpy(s->ptr+s->len, ptr, size*nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size*nmemb;
}

void string_to_UPPERcase(char *target) {
    for(int i = 0; target[i] != 0 ;i++) {
        if(target[i] <= 'z' && target[i] >= 'a') { target[i] &= (~(1<<5));} //the differenc from UPPER case to lower case is 32 so we unset the 5th bit 
    }
}

void get_input(char *ServiceURL, char *cashboxid, char *accesstoken, char *countryCode) {
    
    //Getting all the input
    //ask for Service URL
    printf("Please enter the serviceurl of the fiskaltrust.Service: ");
    fgets(ServiceURL,STRING_LENGTH-1,stdin);

    //get cashboxID
    printf("Please enter the cashboxid of the fiskaltrust.CashBox: ");
    fgets(cashboxid,STRING_LENGTH-1,stdin);

    //get accesstoken
    printf("Please enter the accesstoken of the fiskaltrust.CashBox: ");
    fgets(accesstoken,STRING_LENGTH-1,stdin);

    //get country Code
    printf("Please enter the country of the fiskaltrust.Queue (e.g. \"AT,DE,FR\"): ");
    fgets(countryCode,STRING_LENGTH-1,stdin);

    //trim the input strings
    trim(ServiceURL, NULL);
    trim(cashboxid, NULL);
    trim(accesstoken, NULL);
    trim(countryCode, NULL);

    string_to_UPPERcase(countryCode);

    //check countyCode length
    if(strlen(countryCode) != 2) {
        printf("The country must have length two.\n");
        exit(EXIT_FAILURE);
    }

    //if ServiceURL end with '/' -> delete it
    if(ServiceURL[strlen(ServiceURL) -1] == '/') {ServiceURL[strlen(ServiceURL) -1] = 0;}
}

int64_t build_receipt_case(char *countryCode) {
    int64_t receipt_case = 0;

    //add county Code
    receipt_case |= ((int64_t)countryCode[0] << (4 * 14));
    receipt_case |= ((int64_t)countryCode[1] << (4 * 12));

    //zero receipt
    receipt_case |= 2;

    return receipt_case;
}

json_object *set_body(char *cashboxid, int64_t receipt_case) {
    char buffer[128];

    json_object *root = json_object_new_object();
    json_object_object_add(root,"ftCashBoxID",json_object_new_string(cashboxid));
    json_object_object_add(root,"cbTerminalID",json_object_new_string("1"));
    json_object_object_add(root,"cbReceiptReference",json_object_new_string("1"));
    sprintf(buffer,"/Date(%lu)/",(unsigned long)time(NULL));
    json_object_object_add(root,"cbReceiptMoment",json_object_new_string(buffer));
    json_object_object_add(root,"cbChargeItems",json_object_new_array());
    json_object_object_add(root,"cbPayItems",json_object_new_array());
    json_object_object_add(root,"ftReceiptCase",json_object_new_int64(receipt_case));

    //printf("Body:\n %s\n",json_object_to_json_string(root));
    return root;
}

void send_request(char *ServiceURL, char *cashboxid, char *accesstoken, const char *body, struct response *s, int64_t *response_code) {
    
    CURL *curl = NULL;
    CURLcode res;
    char buffer[STRING_LENGTH + 128];
    struct curl_slist *header = NULL;
    
    #ifdef _WIN32
        //init socket for Windows
        curl_global_init(CURL_GLOBAL_ALL);
    #endif

    //init curl
    curl = curl_easy_init();
    
    char cer_path[] = {".\\"};
    char cer_name[] = {"curl-ca-bundle.crt"};
    char requestURL[STRING_LENGTH];
    strcpy(requestURL, ServiceURL);
    strcat(requestURL, "/json/sign"); //add endpoint

    if (curl)
    {
         
        // Add header content
        // Content-Type
        header = curl_slist_append(header, "Content-Type: application/json");

        // Cashbox id
        sprintf(buffer,"cashboxid: %s",cashboxid);
        header = curl_slist_append(header, buffer);

        // Cashbox id
        sprintf(buffer,"accesstoken: %s",accesstoken);
        header = curl_slist_append(header, buffer);

        // set our custom set of headers
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);

        // set our body
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);

        // set our Service Url
        curl_easy_setopt(curl, CURLOPT_URL, requestURL);

        // tell libcurl to follow redirection
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // set verify certificate
        //curl_easy_setopt(curl, CURLOPT_CAINFO, cer_name); //add curl certificate
        //curl_easy_setopt(curl, CURLOPT_CAPATH, cer_path); //path to certificate

         
        // get header with callback
        //curl_easy_setopt(curl, CURLOPT_HEADER, 1);

        // set response
        //set callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, s);

        // Perform the request, res will get the return code
        printf("performing request ...");
        res = curl_easy_perform(curl);
        
        // Check for errors
        if (res != CURLE_OK) {
            printf(" failed\n");
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        }
        else {
            printf(" OK\n");
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
        }
        // always cleanup
        curl_easy_cleanup(curl);
        curl_slist_free_all(header); //free header list
        
    }
    else {
        fprintf(stderr,"ERROR curl easy init failed!\n");
    }
}

int main()
{
    printf("This example sends a sign request to the fiskaltrust.Service via REST\n");
    
    /*
    char ServiceURL[STRING_LENGTH];
    char cashboxid[STRING_LENGTH];
    char accesstoken[STRING_LENGTH];
    char countryCode[STRING_LENGTH];

    get_input(ServiceURL, cashboxid, accesstoken, countryCode);
    */

    //char ServiceURL[] = {"https://signaturcloud-sandbox.fiskaltrust.at"};
    char ServiceURL[] = {"https://fiskaltrust.free.beeceptor.com"};
    char cashboxid[] = {"a37ce376-62be-42c6-b560-1aa0a6700211"};
    char accesstoken[] = {"BJ6ZufH6hcCHmu2yzc9alH45FjdlCUT1YDlAf83gTydHKj1ZWcMibPlheky1WLMc+E9WeHYanQ8vS5oCirhI6Ck="};
    char countryCode[] = {"AT"};
    
    //char body[BODY_SIZE] = {0};
    int64_t response_code;

    //init response struct
    struct response s;
    init_string(&s);

    int64_t receip_case = build_receipt_case(countryCode);

    json_object *body = set_body(cashboxid, receip_case);    
    
    send_request(ServiceURL, cashboxid, accesstoken, json_object_to_json_string(body), &s, &response_code);
    json_object_put(body);

    //print Response
    if(s.ptr[0] == 0) {
        printf("No Response\n");
    }
    else {
        printf("Response Code: %ld\n",response_code);
        printf("Body:\n%s\n", s.ptr);

        //parse response body to json
        json_object *target = NULL;
        json_object *response_json = json_tokener_parse(s.ptr);
        json_pointer_get(response_json,"/ftState",&target);
        printf("ftState: %s\n",json_object_to_json_string(target));

        free(s.ptr);
    }
    return 0;
}